#include "acquire.h"
#include "device/hal/device.manager.h"
#include "logger.h"

#include <cstdio>
#include <stdexcept>
#include <cstring>

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

void
acquire(AcquireRuntime* runtime,
        struct AcquireProperties* props,
        const char* external_metadata_json)
{
    storage_properties_init(&props->video[0].storage.settings,
                            0,
                            nullptr,
                            0,
                            external_metadata_json,
                            strlen(external_metadata_json) + 1,
                            { 0, 0 });

    OK(acquire_configure(runtime, props));
    OK(acquire_start(runtime));
    OK(acquire_stop(runtime));

    LOG(R"(Done "%s")", external_metadata_json);
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

    DEVOK(device_manager_select(dm,
                                DeviceKind_Camera,
                                SIZED("simulated.*empty") - 1,
                                &props.video[0].camera.identifier));
    DEVOK(device_manager_select(dm,
                                DeviceKind_Storage,
                                SIZED("Trash") - 1,
                                &props.video[0].storage.identifier));

    props.video[0].camera.settings.binning = 1;
    props.video[0].camera.settings.pixel_type = SampleType_u8;
    props.video[0].camera.settings.shape = { .x = 64, .y = 48 };
    props.video[0].max_frame_count = 7;

    acquire(runtime, &props, R"({"hello": "world"})");
    acquire(runtime, &props, R"({"foo": "bar"})");
    acquire(runtime, &props, R"({"hurley": "burley"})");
    acquire(runtime, &props, R"({})");

    LOG("DONE (OK)");
    acquire_shutdown(runtime);
    return 0;
}
