/// If, during acquisition, we have dropped any frames, as determined by a gap
/// in the sequence of frame IDs, acquisition should NOT abort.

#include "acquire.h"
#include "device/hal/device.manager.h"
#include "platform.h"
#include "logger.h"

#include <cstdio>
#include <stdexcept>
#include <string>

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

/// Check that a==b
/// example: `ASSERT_EQ(int,"%d",42,meaning_of_life())`
#define ASSERT_EQ(T, fmt, a, b)                                                \
    do {                                                                       \
        T a_ = (T)(a);                                                         \
        T b_ = (T)(b);                                                         \
        EXPECT(a_ == b_, "Expected %s==%s but " fmt "!=" fmt, #a, #b, a_, b_); \
    } while (0)

/// Check that a!=b
/// example: `ASSERT_NEQ(int,"%d",42,meaning_of_life())`
#define ASSERT_NEQ(T, fmt, a, b)                                               \
    do {                                                                       \
        T a_ = (T)(a);                                                         \
        T b_ = (T)(b);                                                         \
        EXPECT(a_ != b_, "Expected %s!=%s but " fmt "==" fmt, #a, #b, a_, b_); \
    } while (0)

#define OK(e) CHECK(AcquireStatus_Ok == (e))
#define DEVOK(e) CHECK(Device_Ok == (e))

#define SIZED(str) str, sizeof(str) - 1

static class IntrospectiveLogger
{
  public:
    IntrospectiveLogger()
      : dropped_logs(0){};

    // inspect for "[stream 0] Dropped", otherwise pass the mesage through
    void report_and_inspect(int is_error,
                            const char* file,
                            int line,
                            const char* function,
                            const char* msg)
    {
        std::string m(msg);
        if (m.length() >= 18 && m.substr(0, 18) == "[stream 0] Dropped")
            ++dropped_logs;

        printf("%s%s(%d) - %s: %s\n",
               is_error ? "ERROR " : "",
               file,
               line,
               function,
               msg);
    }

    [[nodiscard]] bool frames_were_dropped() const { return dropped_logs > 0; }

  private:
    size_t dropped_logs;
} introspective_logger;

static void
reporter(int is_error,
         const char* file,
         int line,
         const char* function,
         const char* msg)
{
    introspective_logger.report_and_inspect(
      is_error, file, line, function, msg);
}

void
acquire(AcquireRuntime* runtime, const AcquireProperties& props)
{
    struct clock timeout = {};
    static double time_limit_ms = 10000.0;
    clock_init(&timeout);
    clock_shift_ms(&timeout, time_limit_ms);

    uint64_t nframes = 0;
    const auto next = [](VideoFrame* cur) -> VideoFrame* {
        return (VideoFrame*)(((uint8_t*)cur) + cur->bytes_of_frame);
    };

    VideoFrame *beg, *end, *cur;

    OK(acquire_start(runtime));
    while (nframes < props.video[0].max_frame_count &&
           DeviceState_Running == acquire_get_state(runtime)) {
        EXPECT(clock_cmp_now(&timeout) < 0, "Ran out of time.");
        OK(acquire_map_read(runtime, 0, &beg, &end));
        for (cur = beg; cur < end; cur = next(cur))
            ++nframes;
        clock_sleep_ms(nullptr, 10.0);
        OK(acquire_unmap_read(runtime, 0, (uint8_t*)end - (uint8_t*)beg));
    }

    do {
        OK(acquire_map_read(runtime, 0, &beg, &end));
        for (cur = beg; cur < end; cur = next(cur))
            ++nframes;
        OK(acquire_unmap_read(runtime, 0, (uint8_t*)end - (uint8_t*)beg));
    } while (beg != end);

    OK(acquire_stop(runtime));

    // even though we expect to have dropped some frames, the runtime must not
    // have aborted!
    ASSERT_EQ(
      unsigned long long, "%llu", nframes, props.video[0].max_frame_count);
    CHECK(introspective_logger.frames_were_dropped());
}

int
main()
{
    // init
    AcquireRuntime* runtime;
    CHECK(runtime = acquire_init(reporter));

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

        props.video[0].camera.settings.binning = 1;
        props.video[0].camera.settings.shape = { .x = 1 << 14, .y = 1 << 14 };
        // The simulated camera will run as fast as it can
        props.video[0].camera.settings.exposure_time_us = 1;
        props.video[0].max_frame_count = 100;

        OK(acquire_configure(runtime, &props));
    }

    acquire(runtime, props);

    // cleanup
    OK(acquire_shutdown(runtime));
    return 0;
}
