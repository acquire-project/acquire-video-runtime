#include "acquire.h"
#include "device/hal/device.manager.h"
#include "platform.h"
#include "logger.h"

#include <cstdio>
#include <cmath>
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
#define LOGE(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define EXPECT(e, ...)                                                         \
    do {                                                                       \
        if (!(e)) {                                                            \
            char buf[1 << 8] = { 0 };                                          \
            LOGE(__VA_ARGS__);                                                  \
            snprintf(buf, sizeof(buf) - 1, __VA_ARGS__);                       \
            throw std::runtime_error(buf);                                     \
        }                                                                      \
    } while (0)
#define CHECK(e) EXPECT(e, "Expression evaluated as false: %s", #e)
#define DEVOK(e) CHECK(Device_Ok == (e))
#define OK(e) CHECK(AcquireStatus_Ok == (e))

/// Check that the absolute difference between two doubles is within some tolerance.
/// example: `assert_within_abs(1.1, 1.12, 0.1)` passes
void
assert_within_abs(double actual, double expected, double tolerance)
{
    double abs_diff = std::fabs(expected - actual);
    EXPECT(
        abs_diff < tolerance,
        "Expected (%g) ~= (%g) but the absolute difference %g is greater than the tolerance %g",
        actual, expected, abs_diff, tolerance); 

}

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
                                SIZED("Trash") - 1,
                                &props.video[0].storage.identifier));

    // Configure a frame averaging filter to compute the average of
    // every 2 frames.
    props.video[0].frame_average_count = 2;

    OK(acquire_configure(runtime, &props));

    AcquirePropertyMetadata metadata = { 0 };
    OK(acquire_get_configuration_metadata(runtime, &metadata));

    props.video[0].camera.settings.binning = 1;
    props.video[0].camera.settings.pixel_type = SampleType_u8;
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
    // 10 * expected time to acquire frames
    const double time_limit_ms = 
        props.video[0].max_frame_count
        * (props.video[0].camera.settings.exposure_time_us / 1000.0)
        * 10;
    
   
    clock_init(&clock);
    clock_shift_ms(&clock, time_limit_ms);
    OK(acquire_start(runtime));
    {
        uint64_t nframes = 0;
        const uint64_t expected_nframes = props.video[0].max_frame_count / props.video[0].frame_average_count;
        LOG("Expecting %d frames", expected_nframes);

        // Each pixel is drawn from a uniform distribution in [0, 255].
        // Without averaging we would expect the within-frame pixel value
        // variance to follow that of a discrete uniform distribution:
        // (256^2 - 1) / 12.
        // By averaging over every two frames, this shrinks by a factor of 2:
        // (256^2 - 1) / 24
        const double expected_pixel_variance = 2730.625;  // (256*256 - 1) / 24
        const size_t num_pixels = props.video[0].camera.settings.shape.x * props.video[0].camera.settings.shape.y;
        const double normalization_factor = 1.0 / (num_pixels * expected_nframes);
        double actual_pixel_mean = 0;
        double actual_pixel_sum_of_squares = 0;

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
                float * data = (float *)&(cur->data[0]);
                for (size_t i=0; i < num_pixels; ++i) {
                    const double value = (double)data[i];
                    actual_pixel_mean += normalization_factor * value;
                    actual_pixel_sum_of_squares += normalization_factor * value * value;
                }
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

        CHECK(nframes == expected_nframes);
        // Our tolerance is a little loose since the pixel values are high
        // and we're only averaging over every two frames.
        double actual_pixel_variance = actual_pixel_sum_of_squares - (actual_pixel_mean * actual_pixel_mean);
        LOGE("pixel variance: actual = %g, expected = %g", actual_pixel_variance, expected_pixel_variance);
        assert_within_abs(actual_pixel_variance, expected_pixel_variance, 10);
    }

    OK(acquire_stop(runtime));
    OK(acquire_shutdown(runtime));
    return 0;
}
