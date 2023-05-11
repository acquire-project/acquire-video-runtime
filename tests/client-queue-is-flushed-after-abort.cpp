/// Frames acquired in a failed run should not remain on the queue when the next
/// acquisition begins.

#include "acquire.h"
#include "device/hal/device.manager.h"
#include "platform.h"
#include "logger.h"

#include <cstdio>
#include <stdexcept>

void
reporter(int is_error,
         const char* file,
         int line,
         const char* function,
         const char* msg)
{
    fprintf(is_error ? stderr : stdout,
            "%s%s(%d) - %s: %s\n",
            is_error ? "ERROR " : "",
            file,
            line,
            function,
            msg);
}

#define SIZED(str) str, sizeof(str)

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ERR(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            char buf[1 << 8] = { 0 };                                          \
            ERR(__VA_ARGS__);                                                  \
            snprintf(buf, sizeof(buf) - 1, __VA_ARGS__);                       \
            throw std::runtime_error(buf);                                     \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false: %s", #e)
#define DEVOK(e) CHECK(Device_Ok == (e))
#define AOK(e) CHECK(AcquireStatus_Ok == (e))

void
repeat_start_no_stop(AcquireRuntime* runtime)
{
    const DeviceManager* dm;
    CHECK(dm = acquire_device_manager(runtime));

    AcquireProperties properties = {};
    {
        AOK(acquire_get_configuration(runtime, &properties));
        DEVOK(device_manager_select(dm,
                                    DeviceKind_Camera,
                                    SIZED("simulated.*empty.*") - 1,
                                    &properties.video[0].camera.identifier));
        DEVOK(device_manager_select(dm,
                                    DeviceKind_Storage,
                                    SIZED("Trash") - 1,
                                    &properties.video[0].storage.identifier));

        properties.video[0].camera.settings.binning = 1;
        properties.video[0].camera.settings.exposure_time_us = 1e4;
        properties.video[0].max_frame_count = 1 << 30;

        AOK(acquire_configure(runtime, &properties));
    }

    AOK(acquire_start(runtime));

    // await some data
    {
        VideoFrame *beg = 0, *end = 0;
        while (beg == end) {
            acquire_map_read(runtime, 0, &beg, &end);
            clock_sleep_ms(nullptr, 50.0);
        }
        acquire_unmap_read(runtime, 0, (uint8_t*)end - (uint8_t*)beg);
    }

    CHECK(AcquireStatus_Error == acquire_start(runtime));
    AOK(acquire_abort(runtime));
}

void
two_video_streams(AcquireRuntime* runtime)
{
    auto dm = acquire_device_manager(runtime);
    CHECK(dm);

    AcquireProperties props = {};
    AOK(acquire_get_configuration(runtime, &props));

    DEVOK(device_manager_select(dm,
                                DeviceKind_Camera,
                                SIZED("simulated.*empty.*") - 1,
                                &props.video[0].camera.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Camera,
                                SIZED("simulated.*empty.*") - 1,
                                &props.video[1].camera.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Storage,
                                SIZED("Trash") - 1,
                                &props.video[0].storage.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Storage,
                                SIZED("Trash") - 1,
                                &props.video[1].storage.identifier));

    props.video[0].camera.settings.binning = 1;
    props.video[0].camera.settings.pixel_type = SampleType_u8;
    props.video[0].camera.settings.shape = { .x = 64, .y = 64 };
    // we may drop frames with lower exposure
    props.video[0].camera.settings.exposure_time_us = 1e4;
    props.video[0].max_frame_count = 90;

    props.video[1].camera.settings = props.video[0].camera.settings;
    props.video[1].camera.settings.shape = { .x = 64, .y = 64 };
    // we may drop frames with lower exposure
    props.video[1].camera.settings.exposure_time_us = 1e4;
    props.video[1].max_frame_count = 70;

    AOK(acquire_configure(runtime, &props));

    const auto next = [](VideoFrame* cur) -> VideoFrame* {
        return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
    };

    const auto consumed_bytes = [](const VideoFrame* const cur,
                                   const VideoFrame* const end) -> size_t {
        return (uint8_t*)end - (uint8_t*)cur;
    };

    struct clock clock
    {};
    static double time_limit_ms = 20000.0;
    clock_init(&clock);
    clock_shift_ms(&clock, time_limit_ms);
    AOK(acquire_start(runtime));
    {
        int istream = 0;
        uint64_t nframes[2] = { 0, 0 };
        while ((nframes[0] < props.video[0].max_frame_count) ||
               (nframes[1] < props.video[1].max_frame_count)) {
            if (nframes[istream] < props.video[istream].max_frame_count) {
                struct clock throttle
                {};
                clock_init(&throttle);
                EXPECT(clock_cmp_now(&clock) < 0,
                       "Timeout at %f ms",
                       clock_toc_ms(&clock) + time_limit_ms);
                VideoFrame *beg, *end, *cur;
                AOK(acquire_map_read(runtime, istream, &beg, &end));

                for (cur = beg; cur < end; cur = next(cur)) {
                    EXPECT(nframes[istream] == cur->frame_id,
                           "frame id's didn't match (%u!=%u) [stream "
                           "%d nframes [%u %u]]",
                           (unsigned)cur->frame_id,
                           (unsigned)nframes[istream],
                           istream,
                           (unsigned)nframes[0],
                           (unsigned)nframes[1]);
                    CHECK(cur->shape.dims.width ==
                          props.video[istream].camera.settings.shape.x);
                    CHECK(cur->shape.dims.height ==
                          props.video[istream].camera.settings.shape.y);

                    ++nframes[istream];
                }

                {
                    uint32_t n = (uint32_t)consumed_bytes(beg, end);
                    AOK(acquire_unmap_read(runtime, istream, n));
                }
                clock_sleep_ms(&throttle, 100.0f);
            }
            istream = (istream + 1) % 2;
        }

        CHECK(nframes[0] == props.video[0].max_frame_count);
        CHECK(nframes[1] == props.video[1].max_frame_count);
    }

    AOK(acquire_stop(runtime));
}

int
main()
{
    AcquireRuntime* runtime = acquire_init(reporter);
    repeat_start_no_stop(runtime);
    two_video_streams(runtime);
    AOK(acquire_shutdown(runtime));
}
