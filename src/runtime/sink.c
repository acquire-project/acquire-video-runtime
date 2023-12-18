#include "sink.h"
#include "vfslice.h"
#include "platform.h"
#include "logger.h"
#include "throttler.h"
#include "device/hal/storage.h"
#include <string.h>

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// #define TRACE(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define TRACE(...)

#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE(__VA_ARGS__);                                                 \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false:\n\t%s", #e)

static int
is_equal(const struct DeviceIdentifier* const a,
         const struct DeviceIdentifier* const b)
{
    return (a->driver_id == b->driver_id) && (a->device_id == b->device_id);
}

enum DeviceStatusCode
video_sink_init(struct video_sink_s* self,
                uint8_t stream_id,
                size_t channel_capacity_bytes,
                void (*sig_stop_source)(const struct video_sink_s*))
{
    memset(self, 0, sizeof(*self));
    self->stream_id = stream_id;
    self->sig_stop_source = sig_stop_source;

    LOG("Video[%2d]: Allocating %llu bytes for the queue.",
        stream_id,
        channel_capacity_bytes);
    channel_new(&self->in, channel_capacity_bytes);

    thread_init(&self->thread);
    return Device_Ok;
Error:
    return Device_Err;
}

static int
video_sink_thread(struct video_sink_s* const self)
{
    TRACE("[stream %d]: SINK: Entering thread", self->stream_id);
    struct throttler throttler = throttler_init(10e-3f);
    struct vfslice slice = { .beg = 0, .end = 0 };

    // Write to storage.
    // Enforce write delay.
    while (!self->is_stopping && self->storage &&
           storage_get_state(self->storage) == DeviceState_Running) {
        do {
            slice = make_vfslice(channel_read_map(&self->in, &self->reader));
            struct vfslice remaining =
              vfslice_split_at_delay_ms(&slice, self->write_delay_ms);
            CHECK(storage_append(self->storage, slice.beg, remaining.beg) ==
                  Device_Ok);
            channel_read_unmap(&self->in,
                               &self->reader,
                               (uint8_t*)remaining.beg - (uint8_t*)slice.beg);
        } while (slice.end > slice.beg);
        throttler_wait(&throttler);
    }
    TRACE("[stream %d]: SINK: Flushing", self->stream_id);
    do {
        slice = make_vfslice(channel_read_map(&self->in, &self->reader));
        CHECK(storage_append(self->storage, slice.beg, slice.end) == Device_Ok);
        channel_read_unmap(
          &self->in, &self->reader, (uint8_t*)slice.end - (uint8_t*)slice.beg);
    } while (slice.end > slice.beg);

    CHECK(storage_stop(self->storage) == Device_Ok);
    LOG("[stream %d]: SINK: Exiting thread", self->stream_id);
    self->is_running = 0;
    self->is_stopping = 0;
    return 0;
Error:
    LOGE("[stream %d]: SINK: Exiting thread (Error)", self->stream_id);
    self->sig_stop_source(self);
    channel_read_unmap(&self->in, &self->reader, 0);
    storage_stop(self->storage);
    self->is_running = 0;
    self->is_stopping = 0;
    return 1;
}

enum DeviceStatusCode
video_sink_start(struct video_sink_s* self)
{
    EXPECT(self->storage,
           "Expected open storage device for video stream %d.",
           self->stream_id);

    EXPECT(storage_get_state(self->storage) == DeviceState_Armed,
           "Storage device should be armed for stream %d. State is %s.",
           self->stream_id,
           device_state_as_string(storage_get_state(self->storage)));
    CHECK(storage_start(self->storage) == Device_Ok);
    EXPECT(storage_get_state(self->storage) == DeviceState_Running,
           "Storage device should be running for stream %d. State is %s.",
           self->stream_id,
           device_state_as_string(storage_get_state(self->storage)));

    channel_accept_writes(&self->in, 1);
    self->is_stopping = 0;
    self->is_running = 1;
    CHECK(
      thread_create(&self->thread, (void (*)(void*))video_sink_thread, self));

    return Device_Ok;
Error:
    return Device_Err;
}

enum DeviceStatusCode
video_sink_get(const struct video_sink_s* const self,
               struct DeviceIdentifier* const identifier,
               struct StorageProperties* const settings,
               float* const write_delay_ms)
{
    *identifier = self->identifier;
    *write_delay_ms = self->write_delay_ms;

    return self->storage ? storage_get(self->storage, settings) : Device_Ok;
}

void
video_sink_destroy(struct video_sink_s* self)
{
    thread_join(&self->thread);
    if (self->storage) {
        storage_close(self->storage);
    }
    channel_release(&self->in);
}

size_t
video_sink_bytes_waiting(const struct video_sink_s* self)
{
    if (self->reader.id > 0) {
        size_t pos = self->in.holds.pos[self->reader.id - 1];
        size_t head = self->in.head;
        size_t high = self->in.high;
        if (pos > head) {
            return (high - pos) + head;
        } else {
            return head - pos;
        }
    }
    return 0;
}

enum DeviceStatusCode
video_sink_configure(struct video_sink_s* self,
                     const struct DeviceManager* device_manager,
                     struct DeviceIdentifier* identifier,
                     struct StorageProperties* settings,
                     float write_delay_ms)
{
    self->write_delay_ms = write_delay_ms;
    self->identifier = *identifier;
    if (self->storage && !is_equal(&self->identifier, identifier)) {
        storage_close(self->storage);
        self->storage = NULL;
    }
    if (!self->storage) {
        CHECK(self->storage = storage_open(device_manager, identifier));
        self->identifier = *identifier;
    }
    CHECK(Device_Ok == storage_set(self->storage, settings));
    return Device_Ok;
Error:
    return Device_Err;
}
