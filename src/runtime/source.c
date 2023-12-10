#include "source.h"

#include "device/hal/camera.h"
#include "logger.h"
#include "platform.h"
#include "runtime/channel.h"

#define LOG(...) aq_logger(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) aq_logger(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// #define TRACE(...) LOG(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
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
bytes_of_image(const struct ImageShape* const shape)
{
    return shape->strides.planes * bytes_of_type(shape->type);
}

static int
check_frame_id(uint8_t stream_id,
               uint64_t iframe,
               uint64_t last_hardware_frame_id,
               const struct ImageInfo* info)
{
    TRACE(
      "iframe: %d, hardware_frame_id: %llu", iframe, info->hardware_frame_id);

    EXPECT(iframe == 0 || info->hardware_frame_id <= last_hardware_frame_id + 1,
           "[stream %d] Dropped %llu frames (last: %llu; latest: %llu)",
           stream_id,
           info->hardware_frame_id - last_hardware_frame_id - 1,
           last_hardware_frame_id,
           info->hardware_frame_id);
    return 1;
Error:
    return 0;
}

static int
video_source_thread(struct video_source_s* self)
{
    int ecode = 0;
    struct ImageInfo info = { 0 };

    uint64_t iframe = 0;
    uint64_t last_hardware_frame_id = 0;
    struct channel* last_stream = 0;
    while (!self->is_stopping && iframe < self->max_frame_count) {
        EXPECT(camera_get_image_shape(self->camera, &info.shape) == Device_Ok,
               "[stream %d] SOURCE: Failed to query image shape",
               (int)self->stream_id);

        size_t sz = bytes_of_image(&info.shape);
        size_t nbytes = sizeof(struct VideoFrame) + sz;

        struct channel* channel =
          (self->enable_filter) ? self->to_filter : self->to_sink;

        if (channel != last_stream && last_stream == self->to_filter) {
            self->await_filter_reset(self);
        }
        last_stream = channel;

        struct VideoFrame* im =
          (struct VideoFrame*)channel_write_map(channel, nbytes);
        if (im) {
            CHECK(camera_get_frame(self->camera, im->data, &sz, &info) ==
                  Device_Ok);
            if (!sz) {
                channel_abort_write(channel);
            } else {
                check_frame_id(
                  self->stream_id, iframe, last_hardware_frame_id, &info);
                last_hardware_frame_id = info.hardware_frame_id;
                *im = (struct VideoFrame){
                    .shape = info.shape,
                    .bytes_of_frame = nbytes,
                    .frame_id = iframe,
                    .hardware_frame_id = info.hardware_frame_id,
                    .timestamps.hardware = info.hardware_timestamp,
                    .timestamps.acq_thread = clock_tic(0)
                };
                ++iframe;
            }
            channel_write_unmap(channel);
            LOG("[stream %d] SOURCE: wrote frame %d",
                (int)self->stream_id,
                (int)iframe);
        }
    }
Finalize:
    LOG("[stream %d] SOURCE: Stopping on frame %d",
        (int)self->stream_id,
        (int)iframe);
    self->sig_stop_filter(self);
    self->sig_stop_sink(self);

    ECHO(camera_stop(self->camera));

    self->is_stopping = 0;
    self->is_running = 0;
    return ecode;
Error:
    ecode = 1;
    goto Finalize;
}

enum DeviceStatusCode
video_source_init(struct video_source_s* self,
                  uint8_t stream_id,
                  uint64_t max_frame_count,
                  struct channel* to_sink,
                  struct channel* to_filter,
                  void (*await_filter_reset)(const struct video_source_s*),
                  void (*sig_stop_filter)(const struct video_source_s*),
                  void (*sig_stop_sink)(const struct video_source_s*))
{
    *self = (struct video_source_s){
        .max_frame_count = max_frame_count,
        .stream_id = stream_id,
        .to_filter = to_filter,
        .to_sink = to_sink,
        .enable_filter = 0,
        .await_filter_reset = await_filter_reset,
        .sig_stop_filter = sig_stop_filter,
        .sig_stop_sink = sig_stop_sink,
    };
    thread_init(&self->thread);
    return Device_Ok;
}

void
video_source_destroy(struct video_source_s* self)
{
    thread_join(&self->thread);
    if (self->camera)
        camera_close(self->camera);
}

enum DeviceStatusCode
video_source_get(const struct video_source_s* self,
                 struct DeviceIdentifier* source_device_identifier,
                 struct CameraProperties* settings,
                 uint64_t* max_frame_count)
{
    *max_frame_count = self->max_frame_count;
    *source_device_identifier = self->last_camera_id;
    return self->camera ? camera_get(self->camera, settings) : Device_Ok;
}

enum DeviceStatusCode
video_source_start(struct video_source_s* self)
{
    EXPECT(
      self->camera, "Expect open camera for video stream %d.", self->stream_id);

    EXPECT(camera_get_state(self->camera) == DeviceState_Armed,
           "Camera should be armed for stream %d. State is %s.",
           self->stream_id,
           device_state_as_string(camera_get_state(self->camera)));
    CHECK(camera_start(self->camera) == Device_Ok);
    EXPECT(camera_get_state(self->camera) == DeviceState_Running,
           "Camera should be running for stream %d. State is %s.",
           self->stream_id,
           device_state_as_string(camera_get_state(self->camera)));

    self->is_stopping = 0;
    self->is_running = 1;
    CHECK(
      thread_create(&self->thread, (void (*)(void*))video_source_thread, self));
    return Device_Ok;
Error:
    return Device_Err;
}

static int
is_equal(const struct DeviceIdentifier* const a,
         const struct DeviceIdentifier* const b)
{
    return (a->driver_id == b->driver_id) && (a->device_id == b->device_id);
}

static unsigned
try_camera_set(struct video_source_s* const self,
               struct CameraProperties* settings)
{
    // sometimes it takes a couple iterations for properties changes to converge
    int try_count = 0;
    while (try_count < 2 && camera_set(self->camera, settings) != Device_Ok) {
        CHECK(camera_get(self->camera, settings) == Device_Ok);
        ++try_count;
    }
    if (try_count == 2) {
        LOGE("Failed to apply camera properties");
        goto Error;
    }
    CHECK(camera_get(self->camera, settings) == Device_Ok);
    return 1;
Error:
    return 0;
}

enum DeviceStatusCode
video_source_configure(struct video_source_s* self,
                       const struct DeviceManager* const device_manager,
                       struct DeviceIdentifier* identifier,
                       struct CameraProperties* settings,
                       uint64_t max_frame_count,
                       uint8_t enable_filter)
{
    self->max_frame_count = max_frame_count;
    self->enable_filter = enable_filter;
    if (self->camera && !is_equal(&self->last_camera_id, identifier)) {
        camera_close(self->camera);
        self->camera = 0;
    }
    if (!self->camera) {
        CHECK(self->camera = camera_open(device_manager, identifier));
        self->last_camera_id = *identifier;
    }
    CHECK(try_camera_set(self, settings));
    return Device_Ok;
Error:
    return Device_Err;
}
