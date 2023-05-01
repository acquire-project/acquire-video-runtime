#include "acquire.h"
#include "device/hal/device.manager.h"
#include "logger.h"

#include <exception>
#include <stdexcept>
#include <cstdio>

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ERR(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define CHECK(e)                                                               \
    do {                                                                       \
        if (!(e)) {                                                            \
            ERR("Expression was false:\n\t%s\n", #e);                          \
            throw std::runtime_error("Expression was false: " #e);             \
        }                                                                      \
    } while (0)

void
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

int
main()
{
    // init and shutdown
    struct AcquireRuntime* runtime = 0;
    try {
        CHECK(runtime = acquire_init(reporter));
        const struct DeviceManager* dm;
        CHECK(dm = acquire_device_manager(runtime));
        CHECK(AcquireStatus_Ok == acquire_shutdown(runtime));
        dm = 0;
        runtime = 0;

        // reinit and try to configure
        CHECK(runtime = acquire_init(reporter));
        CHECK(dm = acquire_device_manager(runtime));

        struct AcquireProperties props = { 0 };
        CHECK(AcquireStatus_Ok == acquire_get_configuration(runtime, &props));
        CHECK(Device_Ok ==
              device_manager_select(dm,
                                    DeviceKind_Camera,
                                    "simulated: empty",
                                    16,
                                    &props.video[0].camera.identifier));
        CHECK(Device_Ok ==
              device_manager_select(dm,
                                    DeviceKind_Storage,
                                    "Trash",
                                    5,
                                    &props.video[0].storage.identifier));
        props.video[0].camera.settings.binning = 1;
        props.video[0].camera.settings.exposure_time_us = 1e4f;
        props.video[0].max_frame_count = 10;

        CHECK(AcquireStatus_Ok == acquire_configure(runtime, &props));

        return 0;
    } catch (const std::exception& e) {
        ERR("Exception: %s", e.what());
    } catch (...) {
        ERR("Exception: (unknown)");
    }
    return 1;
}
