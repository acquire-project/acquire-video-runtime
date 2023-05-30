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
#define SIZED(str) str, sizeof(str) - 1

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

    DEVOK(device_manager_select(dm,
                                DeviceKind_Camera,
                                SIZED("simulated.*random.*"),
                                &props.video[0].camera.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Camera,
                                SIZED("simulated.*sin.*"),
                                &props.video[1].camera.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Storage,
                                SIZED("tiff"),
                                &props.video[0].storage.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Storage,
                                SIZED("tiff"),
                                &props.video[1].storage.identifier));

    const char external_metadata[] = R"({"hello":"world"})";
    const char filenames[2][100] = { TEST "-1.tif", TEST "-2.tif" };
    const struct PixelScale px_scale_um = { .x = 0.2, .y = 0.2 };

    CHECK(storage_properties_init(&props.video[0].storage.settings,
                                  0,
                                  filenames[0],
                                  sizeof(filenames[0]),
                                  external_metadata,
                                  sizeof(external_metadata),
                                  px_scale_um));

    CHECK(storage_properties_init(&props.video[1].storage.settings,
                                  0,
                                  filenames[1],
                                  sizeof(filenames[1]),
                                  external_metadata,
                                  sizeof(external_metadata),
                                  { .x = 0, .y = 0 }));

    props.video[0].camera.settings.binning = 1;
    props.video[0].camera.settings.pixel_type = SampleType_u8;
    props.video[0].camera.settings.shape = { .x = 64, .y = 48 };
    props.video[0].camera.settings.exposure_time_us = 1e4;
    props.video[0].max_frame_count = 11;

    props.video[1].camera.settings = props.video[0].camera.settings;
    props.video[1].camera.settings.shape = { .x = 32, .y = 32 };
    props.video[1].max_frame_count = 5;

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
    static double time_limit_ms = 20000.0;
    clock_init(&clock);
    clock_shift_ms(&clock, time_limit_ms);
    OK(acquire_start(runtime));
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
                OK(acquire_map_read(runtime, istream, &beg, &end));
                for (cur = beg; cur < end; cur = next(cur)) {
                    LOG("stream %d counting frame w id %d",
                        istream,
                        cur->frame_id);
                    CHECK(cur->shape.dims.width ==
                          props.video[istream].camera.settings.shape.x);
                    CHECK(cur->shape.dims.height ==
                          props.video[istream].camera.settings.shape.y);
                    ++nframes[istream];
                }
                {
                    uint32_t n = (uint32_t)consumed_bytes(beg, end);
                    OK(acquire_unmap_read(runtime, istream, n));
                    if (n)
                        LOG("stream %d consumed bytes %d", istream, n);
                }
                clock_sleep_ms(&throttle, 100.0f);

                LOG("stream %d nframes %d time %f",
                    istream,
                    nframes[istream],
                    clock_toc_ms(&clock));
            }
            istream = (istream + 1) % 2;
        }

        CHECK(nframes[0] == props.video[0].max_frame_count);
        CHECK(nframes[1] == props.video[1].max_frame_count);
    }

    OK(acquire_stop(runtime));
    acquire_shutdown(runtime);
    return 0;
}
