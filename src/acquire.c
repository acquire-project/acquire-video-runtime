#include "acquire.h"

#include "device/hal/camera.h"
#include "logger.h"
#include "platform.h"
#include "runtime/channel.h"
#include "runtime/video.h"
#include "runtime/vfslice.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define containerof(ptr, T, V) ((T*)(((char*)(ptr)) - offsetof(T, V)))
#define countof(e) (sizeof(e) / sizeof(*(e)))

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOGE(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

// #define TRACE(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define TRACE(...)

#if 0
#define ECHO(e)                                                                \
    TRACE("ECHO " #e);                                                         \
    (e)
#else
#define ECHO(e) (e)
#endif

#define CHECK(e)                                                               \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE("Expression evaluated as false:\n\t%s", #e);                  \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            LOGE(__VA_ARGS__);                                                 \
            goto Error;                                                        \
        }                                                                      \
    } while (0)
#define CHECK_SILENT(e)                                                        \
    do {                                                                       \
        if (!(e)) {                                                            \
            goto Error;                                                        \
        }                                                                      \
    } while (0)

struct runtime
{
    struct AcquireRuntime handle;
    enum DeviceState state;
    struct DeviceManager device_manager;

    uint8_t valid_video_streams; /// i'th bit set iff i'th video stream is valid

    struct video_s video[2];
};

#define QUOTE(name) #name
#define STR(macro) QUOTE(macro)

const char*
acquire_api_version_string()
{
    static int inited = 0;
    static char version[1024] = { 0 };
    if (!inited) {
        snprintf(version, // NOLINT
                 sizeof(version),
                 "Version %s-%s by %s",
                 STR(GIT_TAG),
                 STR(GIT_HASH),
                 "acquire");
        inited = 1;
    }
    return version;
}

enum AcquireStatusCode
acquire_map_read(const struct AcquireRuntime* self_,
                 uint32_t istream,
                 struct VideoFrame** beg,
                 struct VideoFrame** end)
{
    struct runtime* self = 0;
    size_t nbytes = 0;

    EXPECT(self_, "Invalid parameter: `self` was NULL.");
    EXPECT(beg, "Invalid parameter: `beg` was NULL.");
    EXPECT(end, "Invalid parameter: `end` was NULL.");
    EXPECT(istream < countof(self->video),
           "Invalid parameter: `istream` was out-of-bounds (%d).",
           countof(self->video));
    self = containerof(self_, struct runtime, handle);
    EXPECT(self->video[istream].monitor.reader.state == ChannelState_Unmapped,
           "Expected an unmapped reader. See acquire_unmap_read().");
    struct vfslice_mut slice = make_vfslice_mut(channel_read_map(
      &self->video[istream].sink.in, &self->video[istream].monitor.reader));
    CHECK(self->video[istream].monitor.reader.status == Channel_Ok);
    *beg = slice.beg;
    *end = slice.end;
    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

enum AcquireStatusCode
acquire_unmap_read(const struct AcquireRuntime* self_,
                   uint32_t istream,
                   size_t consumed_bytes)
{
    struct runtime* self = 0;
    CHECK(self_);
    CHECK(istream < countof(self->video));
    self = containerof(self_, struct runtime, handle);
    channel_read_unmap(&self->video[istream].sink.in,
                       &self->video[istream].monitor.reader,
                       consumed_bytes);
    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

static void
sig_sink_stop_source(const struct video_sink_s* sink)
{
    // This is a pretty hacky way of signaling a video stream to stop at
    // the source.
    struct video_s* self = containerof(sink, struct video_s, sink);
    self->source.is_stopping = 1;
}

static void
await_filter_reset(const struct video_source_s* source)
{
    struct video_s* self = containerof(source, struct video_s, source);
    self->filter.sig_accumulator_reset = 1;
    event_wait(&self->filter.accumulator_reset_event);
}

static void
sig_source_stop_filter(const struct video_source_s* source)
{
    // This is a pretty hacky way of signaling a video stream to stop
    // the filter thread.
    struct video_s* self = containerof(source, struct video_s, source);
    self->filter.is_stopping = 1;
}

static void
sig_source_stop_sink(const struct video_source_s* source)
{
    // This is a pretty hacky way of signaling a video stream to stop
    // the sink thread.
    struct video_s* self = containerof(source, struct video_s, source);
    self->sink.is_stopping = 1;
}

static int
reserve_image_shape(struct video_s* video)
{
    struct ImageShape image_shape = { 0 };
    CHECK(Device_Ok ==
          camera_get_image_shape(video->source.camera, &image_shape));
    CHECK(Device_Ok ==
          storage_reserve_image_shape(video->sink.storage, &image_shape));
    return 1;
Error:
    return 0;
}

struct AcquireRuntime*
acquire_init(void (*reporter)(int is_error,
                              const char* file,
                              int line,
                              const char* function,
                              const char* msg))
{
    struct runtime* self;
    if (!reporter)
        goto Error;
    logger_set_reporter(reporter);

    self = (struct runtime*)malloc(sizeof(struct runtime));
    EXPECT(self,
           "Failed to allocate AcquireRuntime. Requested %llu bytes",
           sizeof(struct runtime));
    memset(self, 0, sizeof(*self)); // NOLINT
    CHECK(device_manager_init(&self->device_manager, reporter) == Device_Ok);

    for (uint8_t i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        memset(video, 0, sizeof(*video)); // NOLINT
        video->stream_id = (uint8_t)i;

        EXPECT(
          video_sink_init(&video->sink, i, 1ULL << 30, sig_sink_stop_source) ==
            Device_Ok,
          "[stream %d] Failed to initialize video sink controller",
          i);
        EXPECT(video_filter_init(
                 &video->filter, i, 1ULL << 30, &video->sink.in) == Device_Ok,
               "[stream %d] Failed to initialize video filter controller",
               i);
        EXPECT(video_source_init(&video->source,
                                 i,
                                 -1,
                                 &video->sink.in,
                                 &video->filter.in,
                                 await_filter_reset,
                                 sig_source_stop_filter,
                                 sig_source_stop_sink) == Device_Ok,
               "[stream %d] Failed to initialize video source controller",
               i);
    }

    self->state = DeviceState_AwaitingConfiguration;
    return &self->handle;
Error:
    return 0;
}

enum AcquireStatusCode
acquire_shutdown(struct AcquireRuntime* self_)
{
    struct runtime* self = 0;
    if (!self_)
        goto Error;
    acquire_abort(self_);
    self = containerof(self_, struct runtime, handle);
    for (size_t i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        video_source_destroy((&video->source));
        video_filter_destroy(&video->filter);
        video_sink_destroy(&video->sink);
    }
    device_manager_destroy(&self->device_manager);
    free(self);
    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

static enum AcquireStatusCode
configure_storage(struct video_s* const video,
                  const struct DeviceManager* const device_manager,
                  struct aq_properties_storage_s* const pstorage)
{
    // Storage
    EXPECT(storage_validate(
             device_manager, &pstorage->identifier, &pstorage->settings),
           "Storage properties failed to validate.");
    video->sink.identifier = pstorage->identifier;
    CHECK(Device_Ok ==
          storage_properties_copy(&video->sink.settings, &pstorage->settings));
    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

static enum AcquireStatusCode
configure_video_stream(struct video_s* const video,
                       enum DeviceState state,
                       const struct DeviceManager* const device_manager,
                       struct aq_properties_video_s* const pvideo

)
{
    struct aq_properties_camera_s* const pcamera = &pvideo->camera;
    struct aq_properties_storage_s* const pstorage = &pvideo->storage;

    int is_ok = 1;
    is_ok &= (video_source_configure(&video->source,
                                     device_manager,
                                     &pcamera->identifier,
                                     &pcamera->settings,
                                     pvideo->max_frame_count) == Device_Ok);
    is_ok &= (video_filter_configure(&video->filter,
                                     pvideo->frame_average_count) == Device_Ok);
    is_ok &=
      (video_sink_configure(&video->sink,
                            device_manager,
                            &pstorage->identifier,
                            &pstorage->settings,
                            (float)pvideo->frame_average_count) == Device_Ok);

    EXPECT(is_ok, "Failed to configure video stream.");

    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

/// Up to two video streams may be optionally configured.
///
/// Performs a cursory check to detect a disabled stream in order to avoid
/// deeper checks. This reduces log chatter.
static int
video_stream_requirements_check(struct aq_properties_video_s* video_settings)
{
    if (video_settings->camera.identifier.kind == DeviceKind_None &&
        video_settings->storage.identifier.kind == DeviceKind_None) {
        return 0;
    }
    return 1;
}

enum AcquireStatusCode
acquire_configure(struct AcquireRuntime* self_,
                  struct AcquireProperties* settings)
{
    struct runtime* self = 0;
    EXPECT(self_, "Invalid parameter. Expected AcquireRuntime* got NULL.");
    EXPECT(settings,
           "Invalid parameter. Expected AcquireProperties* but got NULL.");
    self = containerof(self_, struct runtime, handle);
    EXPECT(self->state != DeviceState_Closed, "Device state is Closed.");
    self->valid_video_streams = 0;
    for (uint32_t istream = 0; istream < countof(self->video); ++istream) {
        if (video_stream_requirements_check(settings->video + istream)) {
            struct video_s* const video = self->video + istream;
            if (AcquireStatus_Ok ==
                configure_video_stream(video,
                                       self->state,
                                       &self->device_manager,
                                       settings->video + istream)) {
                self->valid_video_streams |= (1 << istream);
                TRACE("Configured video stream %d.", istream);
            } else {
                TRACE("Failed to configure video stream %d.", istream);
            }
        }
    }
    TRACE("Valid video streams: code %#04x", self->valid_video_streams);
    self->state = self->valid_video_streams > 0
                    ? DeviceState_Armed
                    : DeviceState_AwaitingConfiguration;
    return AcquireStatus_Ok;
Error:
    if (self_)
        acquire_abort(self_);
    if (self)
        self->state = DeviceState_AwaitingConfiguration;
    return AcquireStatus_Error;
}

enum AcquireStatusCode
acquire_get_configuration(const struct AcquireRuntime* self_,
                          struct AcquireProperties* settings)
{
    struct runtime* self = 0;
    int is_ok = 1;
    EXPECT(self_, "Invalid parameter. Expected AcquireRuntime* got NULL.");
    EXPECT(settings,
           "Invalid parameter. Expected AcquireProperties* got NULL.");
    self = containerof(self_, struct runtime, handle);
    for (uint32_t istream = 0; istream < countof(self->video); ++istream) {
        const struct video_s* const video = self->video + istream;
        struct aq_properties_video_s* const pvideo = settings->video + istream;
        struct aq_properties_camera_s* const pcamera = &pvideo->camera;
        struct aq_properties_storage_s* const pstorage = &pvideo->storage;

        pvideo->frame_average_count = video->filter.filter_window_frames;

        is_ok &= (video_source_get(&video->source,
                                   &pcamera->identifier,
                                   &pcamera->settings,
                                   &pvideo->max_frame_count) == Device_Ok);

        is_ok &= (video_sink_get(&video->sink,
                                 &pstorage->identifier,
                                 &pstorage->settings,
                                 &pstorage->write_delay_ms) == Device_Ok);
    }

    return is_ok ? AcquireStatus_Ok : AcquireStatus_Error;
Error:
    return AcquireStatus_Error;
}

enum AcquireStatusCode
acquire_get_configuration_metadata(const struct AcquireRuntime* self_,
                                   struct AcquirePropertyMetadata* metadata)
{
    struct runtime* self = 0;
    CHECK(self_);
    CHECK(metadata);
    self = containerof(self_, struct runtime, handle);
    for (int i = 0; i < countof(metadata->video); ++i) {
        if (self->video[i].source.camera)
            camera_get_meta(self->video[i].source.camera,
                            &metadata->video[i].camera);
        if (self->video[i].sink.storage)
            storage_get_meta(self->video[i].sink.storage,
                             &metadata->video[i].storage);
        metadata->video[i].max_frame_count = (struct Property){
            .writable = 1,
            .low = 0.0f,
            .high = -1.0f, // NOTE: (nclack) Not sure what's right here.
            .type = PropertyType_FixedPrecision
        };
        metadata->video[i].frame_average_count =
          (struct Property){ .writable = 1,
                             .low = 0.0f,
                             .high =
                               -1.0f, // TODO: (nclack) Compute this. Depends on
                                      // the queue and frame size
                             .type = PropertyType_FixedPrecision };
    }

    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

size_t
acquire_bytes_waiting_to_be_written_to_disk(const struct AcquireRuntime* self_,
                                            uint32_t istream)
{
    struct runtime* self = 0;
    CHECK(self_);
    self = containerof(self_, struct runtime, handle);
    CHECK(istream < countof(self->video));
    const struct video_s* video = self->video + istream;
    return video_sink_bytes_waiting(&video->sink);
Error:
    return 0;
}

static uint32_t
count_devices_by_kind(const struct runtime* self, enum DeviceKind target_kind)
{
    uint32_t count = 0;
    {
        uint32_t device_count = device_manager_count(&self->device_manager);
        struct DeviceIdentifier identifier = { 0 };
        for (uint32_t i = 0; i < device_count; ++i) {
            CHECK(Device_Ok ==
                  device_manager_get(&identifier, &self->device_manager, i));
            count += (identifier.kind == target_kind);
        }
    }
    return count;
Error:
    return 0;
}

uint32_t
acquire_get_camera_count(const struct AcquireRuntime* self_)
{
    struct runtime* self = 0;
    if (!self_)
        goto Error;
    self = containerof(self_, struct runtime, handle);

    return count_devices_by_kind(self, DeviceKind_Camera);
Error:
    return 0;
}

unsigned
acquire_get_storage_device_count(const struct AcquireRuntime* self_)
{
    struct runtime* self = 0;
    if (!self_)
        goto Error;
    self = containerof(self_, struct runtime, handle);

    return count_devices_by_kind(self, DeviceKind_Storage);
Error:
    return 0;
}

enum AcquireStatusCode
acquire_get_shape(const struct AcquireRuntime* self_,
                  uint32_t istream,
                  struct ImageShape* shape)
{
    CHECK(self_);
    const struct runtime* const self =
      containerof(self_, struct runtime, handle);
    CHECK(istream < countof(self->video));
    const struct video_s* const video = self->video + istream;

    CHECK_SILENT(self->state != DeviceState_AwaitingConfiguration);
    CHECK(camera_get_image_shape(video->source.camera, shape) == Device_Ok);
    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

enum AcquireStatusCode
acquire_start(struct AcquireRuntime* self_)
{
    struct runtime* self = containerof(self_, struct runtime, handle);

    EXPECT(self->valid_video_streams > 0,
           "At least one video stream must be marked valid");

    for (int i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("Skipping unconfirmed video stream %d", i);
            continue;
        }

        CHECK(video_sink_start(&video->sink, &self->device_manager) ==
              Device_Ok);
        CHECK(reserve_image_shape(video));
        CHECK(video_filter_start(&video->filter) == Device_Ok);
        CHECK(video_source_start(&video->source) == Device_Ok);

        TRACE("START[%2d] sink:%d processing:%d camera:%d",
              i,
              video->sink.is_running,
              video->filter.is_running,
              video->source.is_running);
    }
    self->state = DeviceState_Running;
    return AcquireStatus_Ok;
Error:
    for (int i = 0; i < countof(self->video); ++i) {
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("(Abort) Skipping disabled video stream %d", i);
            continue;
        }
        struct video_s* video = self->video + i;
        camera_stop(video->source.camera);
    }
    self->state = DeviceState_AwaitingConfiguration;
    return AcquireStatus_Error;
}

static size_t
slice_size_bytes(const struct slice* slice)
{
    return (uint8_t*)slice->end - (uint8_t*)slice->beg;
}

enum AcquireStatusCode
acquire_stop(struct AcquireRuntime* self_)
{
    struct runtime* self = containerof(self_, struct runtime, handle);

    for (size_t i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("Skipping disabled video stream %d", i);
            continue;
        }

        ECHO(thread_join(&video->source.thread));
        ECHO(thread_join(&video->filter.thread));
        ECHO(thread_join(&video->sink.thread));
        channel_accept_writes(&video->sink.in, 1);

        // Flush the monitor's read region if it hasn't already been released.
        // This takes at most 2 iterations.
        {
            size_t nbytes;
            do {
                struct slice slice =
                  channel_read_map(&video->sink.in, &video->monitor.reader);
                nbytes = slice_size_bytes(&slice);
                channel_read_unmap(
                  &video->sink.in, &video->monitor.reader, nbytes);
                TRACE("[stream: %d] Monitor flushed %llu bytes", i, nbytes);
            } while (nbytes);
        }
    }
    self->state = DeviceState_Armed;

    return AcquireStatus_Ok;
}

enum AcquireStatusCode
acquire_abort(struct AcquireRuntime* self_)
{
    struct runtime* self = containerof(self_, struct runtime, handle);

    for (size_t i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("Skipping disabled video stream %d", i);
            continue;
        }

        video->source.is_stopping = 1;
        channel_accept_writes(&video->sink.in, 0);
        camera_stop(video->source.camera);
    }

    return acquire_stop(self_);
}

enum AcquireStatusCode
acquire_execute_trigger(struct AcquireRuntime* self_, uint32_t istream)
{
    const struct runtime* const self =
      containerof(self_, struct runtime, handle);
    const struct video_s* const video = self->video + istream;
    CHECK(self_);
    CHECK(istream < countof(self->video));
    CHECK(video->source.camera);
    CHECK(camera_execute_trigger(video->source.camera) == Device_Ok);
    return AcquireStatus_Ok;
Error:
    return AcquireStatus_Error;
}

enum DeviceState
acquire_get_state(struct AcquireRuntime* self_)
{
    if (!self_)
        return DeviceState_Closed;
    struct runtime* self = containerof(self_, struct runtime, handle);

    if (DeviceState_Running != self->state)
        return self->state;

    // check that at least one pipeline has active threads
    uint8_t is_running = 0;
    for (int i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("Skipping video stream %d", i);
            continue;
        }

        TRACE("source %s running, %s stopping\n"
              "filter %s running, %s stopping\n"
              "  sink %s running, %s stopping",
              video->source.is_running ? "" : "not",
              video->source.is_stopping ? "" : "not",
              video->filter.is_running ? "" : "not",
              video->filter.is_stopping ? "" : "not",
              video->sink.is_running ? "" : "not",
              video->sink.is_stopping ? "" : "not");

        is_running |= video->source.is_running;
        is_running |= video->filter.is_running;
        is_running |= video->sink.is_running;

        if (is_running)
            break;
    }

    return self->state = is_running ? DeviceState_Running : DeviceState_Armed;
}

const struct DeviceManager*
acquire_device_manager(const struct AcquireRuntime* self_)
{
    struct runtime* self = containerof(self_, struct runtime, handle);
    return &self->device_manager;
}

#ifndef NO_UNIT_TESTS
#define OK(e) CHECK(AcquireStatus_Ok == (e))
#define DEV(e) CHECK(Device_Ok == (e))

int
unit_test__monitor_uninitialized_on_stop(
  void (*reporter)(int, const char*, int, const char*, const char*))
{
    struct AcquireRuntime* runtime = 0;
    CHECK(runtime = acquire_init(reporter));
    const struct DeviceManager* dm;
    CHECK(dm = acquire_device_manager(runtime));

    struct AcquireProperties props = { 0 };
    OK(acquire_get_configuration(runtime, &props));
    DEV(device_manager_select(dm,
                              DeviceKind_Camera,
                              "simulated: empty",
                              16,
                              &props.video[0].camera.identifier));
    DEV(device_manager_select(
      dm, DeviceKind_Storage, "Trash", 5, &props.video[0].storage.identifier));
    props.video[0].camera.settings.binning = 1;
    props.video[0].camera.settings.exposure_time_us = 1e4f;
    props.video[0].max_frame_count = 10;

    OK(acquire_configure(runtime, &props));

    struct runtime* self = containerof(runtime, struct runtime, handle);

    // monitor ID is 0 before starting
    for (size_t i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("Skipping unconfigured video stream %d", i);
            continue;
        }

        CHECK(video->monitor.reader.id == 0);
    }

    // monitor ID is 0 during acquisition
    OK(acquire_start(runtime));
    for (size_t i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("Skipping unconfigured video stream %d", i);
            continue;
        }

        CHECK(video->monitor.reader.id == 0);
    }

    // monitor ID is 0 when stopped
    OK(acquire_stop(runtime));
    for (size_t i = 0; i < countof(self->video); ++i) {
        struct video_s* video = self->video + i;
        if (((self->valid_video_streams >> i) & 1) == 0) {
            TRACE("Skipping unconfigured video stream %d", i);
            continue;
        }

        CHECK(video->monitor.reader.id == 0);
    }
    OK(acquire_shutdown(runtime));
    return 1;
Error:
    acquire_shutdown(runtime);
    return 0;
}
#endif // end NO_UNIT_TESTS
