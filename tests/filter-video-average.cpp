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

/// Helper for passing size static strings as function args.
/// For a function: `f(char*,size_t)` use `f(SIZED("hello"))`.
/// Expands to `f("hello",5)`.
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
#define OK(e) CHECK(AcquireStatus_Ok == (e))

int
main()
{

    auto runtime = acquire_init(reporter);
    auto dm = acquire_device_manager(runtime);
    CHECK(runtime);
    CHECK(dm);

    AcquireProperties props = {};
    OK(acquire_get_configuration(runtime, &props));

    // TODO: use a non-random simulated camera, so that we can check that
    // filter is having an effect.
    DEVOK(device_manager_select(dm,
                                DeviceKind_Camera,
                                SIZED("simulated.*random.*") - 1,
                                &props.video[0].camera.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Storage,
                                SIZED("tiff") - 1,
                                &props.video[0].storage.identifier));

    // Configure a frame averaging filter to compute the average of
    // every 2 frames.
    props.video[0].frame_average_count = 2;

    storage_properties_init(
      &props.video[0].storage.settings, 0, SIZED("out.tif"), 0, 0, { 0 });

    OK(acquire_configure(runtime, &props));

    AcquirePropertyMetadata metadata = { 0 };
    OK(acquire_get_configuration_metadata(runtime, &metadata));

    props.video[0].camera.settings.binning = 1;
    props.video[0].camera.settings.pixel_type = SampleType_u12;
    props.video[0].camera.settings.shape = {
        .x = 1920,
        .y = 1080,
    };
    props.video[0].camera.settings.exposure_time_us = 1e5;
    props.video[0].max_frame_count = 10;

    OK(acquire_configure(runtime, &props));

    const auto next = [](VideoFrame* cur) -> VideoFrame* {
        return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
    };

    const auto consumed_bytes = [](const VideoFrame* const cur,
                                   const VideoFrame* const end) -> size_t {
        return (uint8_t*)end - (uint8_t*)cur;
    };

    struct clock clock
    {};
    // expected time to acquire frames + 100%
    static double time_limit_ms = 
        props.video[0].max_frame_count
        * (props.video[0].camera.settings.exposure_time_us / 1000.0)
        * 2.0;
    clock_init(&clock);
    clock_shift_ms(&clock, time_limit_ms);
    OK(acquire_start(runtime));
    {
        uint64_t nframes = 0;
        const uint64_t expected_nframes = props.video[0].max_frame_count / props.video[0].frame_average_count;
        LOG("Expecting %d frames", expected_nframes);
        while (nframes < expected_nframes) {
            struct clock throttle
            {};
            clock_init(&throttle);
            EXPECT(clock_cmp_now(&clock) < 0,
                   "Timeout at %f ms",
                   clock_toc_ms(&clock) + time_limit_ms);
            VideoFrame *beg, *end, *cur;
            OK(acquire_map_read(runtime, 0, &beg, &end));
            for (cur = beg; cur < end; cur = next(cur)) {
                LOG("stream %d counting frame w id %d", 0, cur->frame_id);
                CHECK(cur->shape.dims.width ==
                      props.video[0].camera.settings.shape.x);
                CHECK(cur->shape.dims.height ==
                      props.video[0].camera.settings.shape.y);
                ++nframes;
            }
            {
                uint32_t n = (uint32_t)consumed_bytes(beg, end);
                OK(acquire_unmap_read(runtime, 0, n));
                if (n)
                    LOG("stream %d consumed bytes %d", 0, n);
            }
            clock_sleep_ms(&throttle, 100.0f);

            LOG("stream %d nframes %d. remaining time %f s",
                0,
                nframes,
                -1e-3 * clock_toc_ms(&clock));
        }

        // TODO: we want a stronger assertion than this since we want to ensure
        // the values were averaged correctly.
        // A simulated camera that outputs constant value frames, where that
        // constant value increases would be suitable.
        CHECK(nframes == expected_nframes);
    }

    OK(acquire_stop(runtime));
    OK(acquire_shutdown(runtime));
    return 0;
}
