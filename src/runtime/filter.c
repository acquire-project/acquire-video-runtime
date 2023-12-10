#include "filter.h"
#include "frame_iterator.h"
#include "platform.h"
#include "logger.h"
#include "vfslice.h"
#include "throttler.h"

#include <string.h>

#define LOG(...) aq_logger(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) aq_logger(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// #define TRACE(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define TRACE(...)
#define ECHO(e)                                                                \
    TRACE("ECHO " #e);                                                         \
    e

#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE(__VA_ARGS__);                                                 \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false:\n\t%s", #e)

static size_t
slice_size_bytes(const struct slice* slice)
{
    return (uint8_t*)slice->end - (uint8_t*)slice->beg;
}

static size_t
bytes_of_image(const struct ImageShape* const shape)
{
    return shape->strides.planes * bytes_of_type(shape->type);
}

static int
assert_consistent_shape(const struct VideoFrame* acc,
                        const struct VideoFrame* in)
{
    int res0 =
      memcmp(&acc->shape.dims, &in->shape.dims, sizeof(acc->shape.dims));
    int res1 = memcmp(
      &acc->shape.strides, &in->shape.strides, sizeof(acc->shape.strides));
    return (res0 == 0) && (res1 == 0);
}

static int
accumulate(struct VideoFrame* acc, const struct VideoFrame* in)
{
    size_t npx = acc->shape.strides.planes; // assumes planes is outer dim
    if (acc->shape.type != SampleType_f32)
        return 0;

    float* x = (float*)acc->data;
    switch (in->shape.type) {
        case SampleType_u8: {
            const uint8_t* y = in->data;
            for (size_t i = 0; i < npx; ++i)
                x[i] += y[i];
            break;
        }
        case SampleType_u10:
        case SampleType_u12:
        case SampleType_u14:
        case SampleType_u16: {
            const uint16_t* y = (uint16_t*)in->data;
            for (size_t i = 0; i < npx; ++i)
                x[i] += y[i];
            break;
        }
        case SampleType_i8: {
            const int8_t* y = (int8_t*)in->data;
            for (size_t i = 0; i < npx; ++i)
                x[i] += y[i];
            break;
        }
        case SampleType_i16: {
            const int16_t* y = (int16_t*)in->data;
            for (size_t i = 0; i < npx; ++i)
                x[i] += y[i];
            break;
        }
        default:
            LOGE("Unsupported pixel type");
            return 0;
    }
    return 1;
}

static void
normalize(struct VideoFrame* acc, float inverse_norm)
{
    float* const x = (float*)acc->data;
    const size_t npx = acc->shape.strides.planes;
    for (size_t i = 0; i < npx; ++i)
        x[i] *= inverse_norm;
}

static int
process_data(struct video_filter_s* self,
             struct VideoFrame** accumulator,
             uint64_t* frame_count)
{
    struct VideoFrame* in = 0;
    {
        struct slice slice = channel_read_map(&self->in, &self->reader);
        struct frame_iterator it = frame_iterator_init(&slice);
        while ((in = frame_iterator_next(&it))) {
            if (!*accumulator) {
                struct ImageShape shape = in->shape;
                shape.type = SampleType_f32;
                size_t bytes_of_accumulator =
                  bytes_of_image(&shape) + sizeof(struct VideoFrame);
                *accumulator = (struct VideoFrame*)channel_write_map(
                  self->out, bytes_of_accumulator);
                if (*accumulator) {
                    *accumulator[0] = (struct VideoFrame){
                        .bytes_of_frame = bytes_of_accumulator,
                        .frame_id = in->frame_id,
                        .shape = shape,
                        .timestamps = in->timestamps,
                    };
                    CHECK(accumulate(*accumulator, in));
                    *frame_count = 1;
                }
            } else {
                if (assert_consistent_shape(*accumulator, in)) {
                    CHECK(accumulate(*accumulator, in));
                    ++*frame_count;
                    if (*frame_count >= self->filter_window_frames) {
                        normalize(*accumulator,
                                  *frame_count ? 1.0f / (*frame_count) : 1.0f);
                        *frame_count = 0;
                        *accumulator = 0;
                        channel_write_unmap(self->out);
                    }
                } else {
                    LOG("FILTER: emitting early -- shape inconsistent");
                    *frame_count = 0;
                    *accumulator = 0;
                    channel_abort_write(self->out);
                }
            }
        }
        channel_read_unmap(&self->in, &self->reader, slice_size_bytes(&slice));
    };

    if (self->sig_accumulator_reset) {
        LOG("FILTER: accumulator reset (%d)", *frame_count);
        if (*accumulator) {
            *accumulator = 0;
            *frame_count = 0;
            channel_abort_write(self->out);
        }
        self->sig_accumulator_reset = 0;
        event_notify_all(&self->accumulator_reset_event);
    }
    return 1;
Error:
    channel_read_unmap(&self->in, &self->reader, 0);
    // reset state
    *frame_count = 0;
    *accumulator = 0;
    channel_write_unmap(self->out);
    return 0;
}

static int
video_filter_thread(struct video_filter_s* self)
{
    int ecode = 0;
    uint64_t frame_count = 0;
    struct VideoFrame* accumulator = 0;
    LOG("[stream %d] PROCESSING: Entering frame processing thread",
        self->stream_id);
    struct throttler throttler = throttler_init(10e-3f);
    while (!self->is_stopping) {
        CHECK(process_data(self, &accumulator, &frame_count));
        throttler_wait(&throttler);
    }
    LOG("[stream: %d] PROCESSING: Flush", self->stream_id);
    CHECK(process_data(self, &accumulator, &frame_count));
Finalize:
    if (accumulator)
        channel_write_unmap(self->out);
    LOG("[stream: %d] PROCESSING: Exiting frame processing thread",
        self->stream_id);
    self->is_running = 0;
    self->is_stopping = 0;
    return ecode;
Error:
    ecode = 1;
    goto Finalize;
}

enum DeviceStatusCode
video_filter_init(struct video_filter_s* self,
                  uint8_t stream_id,
                  size_t channel_size_bytes,
                  struct channel* out)
{
    CHECK(out);
    *self = (struct video_filter_s){ .stream_id = stream_id, .out = out };
    channel_new(&self->in, channel_size_bytes);
    thread_init(&self->thread);
    event_init(&self->accumulator_reset_event);
    return Device_Ok;
Error:
    return Device_Err;
}

void
video_filter_destroy(struct video_filter_s* self)
{
    thread_join(&self->thread);
    event_destroy(&self->accumulator_reset_event);
    channel_release(&self->in);
}

enum DeviceStatusCode
video_filter_configure(struct video_filter_s* self,
                       uint32_t frame_average_count)
{
    self->filter_window_frames = frame_average_count;
    return Device_Ok;
}

enum DeviceStatusCode
video_filter_start(struct video_filter_s* self)
{
    self->is_stopping = 0;
    self->is_running = 1;
    CHECK(
      thread_create(&self->thread, (void (*)(void*))video_filter_thread, self));
    return Device_Ok;
Error:
    return Device_Err;
}
