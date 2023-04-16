#include "acquire.h"
#include "device/hal/device.manager.h"
#include "device/hal/experimental/stage.axis.h"
#include "logger.h"

#include <cstdio>
#include <stdexcept>

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

struct StageAxisIterator
{
    // cppcheck-suppress noExplicitConstructor
    StageAxisIterator(const struct DeviceManager* device_manager);

    DeviceIdentifier get() const;
    bool is_done() const noexcept;
    void next();

  private:
    const struct DeviceManager* device_manager_;
    uint32_t i;
};

StageAxisIterator::StageAxisIterator(const struct DeviceManager* device_manager)
  : device_manager_(device_manager)
  , i(-1)
{
    next();
}

void
StageAxisIterator::next()
{

    bool any = false;
    ++i;
    for (; i < device_manager_count(device_manager_); ++i) {
        DeviceIdentifier id = {};
        CHECK(Device_Ok == device_manager_get(&id, device_manager_, i));
        if ((any |= (id.kind == DeviceKind_StageAxis)))
            break;
    }
}

DeviceIdentifier
StageAxisIterator::get() const
{
    DeviceIdentifier out = {};
    CHECK(!is_done());
    CHECK(Device_Ok == device_manager_get(&out, device_manager_, i));
    return out;
}

bool
StageAxisIterator::is_done() const noexcept
{
    return i >= device_manager_count(device_manager_);
}

void
invalid_identifier_should_fail(struct AcquireRuntime* runtime,
                               const struct DeviceManager* device_manager)
{
    struct DeviceIdentifier identifier = {};
    CHECK(Device_Ok == device_manager_select_first(
                         device_manager, DeviceKind_Camera, &identifier));
    CHECK(identifier.kind == DeviceKind_Camera);
    CHECK(nullptr == stage_axis_open(device_manager, &identifier));
}

int
main()
{
    auto runtime = acquire_init(reporter);
    CHECK(runtime);
    auto device_manager = acquire_device_manager(runtime);
    CHECK(device_manager);

    invalid_identifier_should_fail(runtime, device_manager);

    auto any = false;
    for (auto it = StageAxisIterator(device_manager); !it.is_done();
         it.next()) {
        DeviceIdentifier identifier = it.get();
        auto axis = stage_axis_open(device_manager, &identifier);
        CHECK(axis);
        stage_axis_close(axis);
        any = true;
    }
    if (!any)
        throw std::runtime_error("No stage axis found.");

    acquire_shutdown(runtime);
    return 0;
}
