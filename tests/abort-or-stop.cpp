/// Calling acquire_abort() should immediately terminate acquisition.
/// Calling acquire_stop() should wait until the frame count is reached.

#include "acquire.h"
#include "platform.h"
#include "logger.h"

#include "device/hal/device.manager.h"

#include <cstdio>
#include <stdexcept>

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

#define OK(e) CHECK(AcquireStatus_Ok == (e))
#define DEVOK(e) CHECK(Device_Ok == (e))

#define SIZED(str) str, sizeof(str) - 1

#define ASSERT_EQ(fmt, a, b)                                                   \
    EXPECT((a) == (b),                                                         \
           "Expected " #a " == " #b ". Got: " fmt " != " fmt ".",              \
           (a),                                                                \
           (b))

static void
reporter(int is_error,
         const char* file,
         int line,
         const char* function,
         const char* msg)
{
    printf("%s%s(%d) - %s: %s\n",
           is_error ? "ERROR " : "",
           file,
           line,
           function,
           msg);
}

struct Packet
{
    AcquireRuntime* runtime_;
    struct event started_, aborted_;
    int expect_abort_;
    bool result_;
};

void
acquire(Packet* packet)
{
    try {
        CHECK(packet);
        auto runtime = packet->runtime_;
        auto expect_abort = packet->expect_abort_;

        const DeviceManager* dm;
        CHECK(dm = acquire_device_manager(runtime));

        AcquireProperties props = {};
        {
            OK(acquire_get_configuration(runtime, &props));
            DEVOK(device_manager_select(dm,
                                        DeviceKind_Camera,
                                        SIZED(".*empty"),
                                        &props.video[0].camera.identifier));
            DEVOK(device_manager_select(dm,
                                        DeviceKind_Storage,
                                        SIZED("Trash"),
                                        &props.video[0].storage.identifier));
            OK(acquire_configure(runtime, &props));

            props.video[0].camera.settings.binning = 1;
            props.video[0].max_frame_count = 10;
            props.video[0].camera.settings.exposure_time_us = 1e5;

            OK(acquire_configure(runtime, &props));
        }

        const auto next = [](VideoFrame* cur) -> VideoFrame* {
            return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
        };

        const auto consumed_bytes = [](const VideoFrame* const cur,
                                       const VideoFrame* const end) -> size_t {
            return (uint8_t*)end - (uint8_t*)cur;
        };

        struct clock clock_ = { 0 };
        static double time_limit_ms = 20000.0;
        clock_init(&clock_);
        clock_shift_ms(&clock_, time_limit_ms);

        VideoFrame *beg, *end, *cur;
        OK(acquire_start(runtime));
        event_notify_all(&packet->started_);
        if (packet->expect_abort_)
            event_wait(&packet->aborted_);

        {
            uint64_t nframes = 0;
            do {
                struct clock throttle = { 0 };
                clock_init(&throttle);
                EXPECT(clock_cmp_now(&clock_) < 0,
                       "Timeout at %f ms",
                       clock_toc_ms(&clock_) + time_limit_ms);
                OK(acquire_map_read(runtime, 0, &beg, &end));

                for (cur = beg; cur < end; cur = next(cur)) {
                    ASSERT_EQ("%d",
                              cur->shape.dims.width,
                              props.video[0].camera.settings.shape.x);
                    ASSERT_EQ("%d",
                              cur->shape.dims.height,
                              props.video[0].camera.settings.shape.y);
                    ++nframes;
                }

                {
                    uint32_t n = (uint32_t)consumed_bytes(beg, end);
                    OK(acquire_unmap_read(runtime, 0, n));
                }
                clock_sleep_ms(&throttle, 100.0f);
            } while (nframes < props.video[0].max_frame_count &&
                     DeviceState_Running == acquire_get_state(runtime));

            OK(acquire_map_read(runtime, 0, &beg, &end));

            for (cur = beg; cur < end; cur = next(cur)) {
                CHECK(cur->shape.dims.width ==
                      props.video[0].camera.settings.shape.x);
                CHECK(cur->shape.dims.height ==
                      props.video[0].camera.settings.shape.y);
                ++nframes;
            }

            if (expect_abort) {
                CHECK(nframes < props.video[0].max_frame_count);
            } else {
                CHECK(nframes == props.video[0].max_frame_count);
            }
        }
        packet->result_ = true;
    } catch (const std::runtime_error& e) {
        ERR("Runtime error: %s", e.what());

    } catch (...) {
        ERR("Uncaught exception");
    }
}

int
main()
{
    AcquireRuntime* runtime = 0;
    try {
        thread t_{};
        thread_init(&t_);

        // abort terminates early
        CHECK(runtime = acquire_init(reporter));

        Packet packet{ .runtime_ = runtime,
                       .expect_abort_ = 1,
                       .result_ = false };
        event_init(&packet.started_);
        event_init(&packet.aborted_);

        thread_create(&t_, (void (*)(void*))acquire, &packet);
        event_wait(&packet.started_);
        acquire_abort(runtime);
        event_notify_all(&packet.aborted_);
        thread_join(&t_);
        event_destroy(&packet.started_);
        event_destroy(&packet.aborted_);
        EXPECT(packet.result_ == true, "Something went wrong in 'abort' test.");

        // stop waits until finished
        packet =
          Packet{ .runtime_ = runtime, .expect_abort_ = 0, .result_ = false };
        event_init(&packet.started_);
        event_init(&packet.aborted_);
        thread_create(&t_, (void (*)(void*))acquire, &packet);
        event_wait(&packet.started_);
        acquire_stop(runtime);
        thread_join(&t_);
        event_destroy(&packet.started_);
        event_destroy(&packet.aborted_);
        EXPECT(packet.result_ == true, "Something went wrong in 'stop' test.");

        acquire_shutdown(runtime);
        LOG("OK");
        return 0;
    } catch (const std::runtime_error& e) {
        ERR("Runtime error: %s", e.what());

    } catch (...) {
        ERR("Uncaught exception");
    }
    acquire_shutdown(runtime);
    return 1;
}
