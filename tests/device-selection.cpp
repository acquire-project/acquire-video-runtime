#include "acquire.h"
#include "device/hal/device.manager.h"
#include "logger.h"

#include <cstdio>
#include <cstring>

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ERR(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define CHECK(e)                                                               \
    do {                                                                       \
        if (!(e)) {                                                            \
            ERR("Expression was false:\n\t%s\n", #e);                          \
            goto Error;                                                        \
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
repeated_selection_is_consistent(const struct DeviceManager* device_manager)
{
    // Test repeated selection is consistent
    DeviceIdentifier h1 = { 0 }, h2 = { 0 };
    CHECK(Device_Ok ==
          device_manager_select_first(device_manager, DeviceKind_Camera, &h1));
    CHECK(Device_Ok ==
          device_manager_select(
            device_manager, DeviceKind_Camera, h1.name, strlen(h1.name), &h2));
    CHECK(0 == memcmp(&h1, &h2, sizeof(h1)));
    return 1;
Error:
    return 0;
}

int
empty_name_selects_first_of_kind(const struct DeviceManager* device_manager)
{
    DeviceIdentifier h1 = { 0 }, h2 = { 0 }, h3 = { 0 };

    CHECK(Device_Ok ==
          device_manager_select(device_manager, DeviceKind_Storage, 0, 0, &h1));

    CHECK(Device_Ok == device_manager_select(
                         device_manager, DeviceKind_Storage, "", 0, &h2));
    CHECK(0 == memcmp(&h1, &h2, sizeof(h1)));

    CHECK(Device_Ok ==
          device_manager_select_first(device_manager, DeviceKind_Storage, &h3));
    CHECK(0 == memcmp(&h1, &h3, sizeof(h1)));
    return 1;
Error:
    return 0;
}

int
null_name_with_bytes_should_fail(const struct DeviceManager* device_manager)
{
    DeviceIdentifier h = { 0 };
    CHECK(Device_Err ==
          device_manager_select(device_manager, DeviceKind_Storage, 0, 10, &h));
    CHECK(h.kind == DeviceKind_None);
    return 1;
Error:
    return 0;
}

int
main(int n, char** args)
{
    auto runtime = acquire_init(reporter);
    CHECK(runtime);
    {
        auto device_manager = acquire_device_manager(runtime);
        CHECK(device_manager);

        CHECK(repeated_selection_is_consistent(device_manager));
        CHECK(empty_name_selects_first_of_kind(device_manager));
        CHECK(null_name_with_bytes_should_fail(device_manager));
    }
    acquire_shutdown(runtime);
    return 0;
Error:
    return 1;
}
