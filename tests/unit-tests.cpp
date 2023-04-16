#include "acquire.h"
#include "logger.h"

#include <cstdio>
#include <vector>

#define L (aq_logger)
#define LOG(...) L(0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define ERR(...) L(1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

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

typedef void (*reporter_t)(int, const char*, int, const char*, const char*);

//
//      TEST DECLARATIONS
//

extern "C"
{
    int unit_test__device_state_as_string__is_defined_for_all();
    int unit_test__device_kind_as_string__is_defined_for_all();
    int unit_test__storage__storage_property_string_check();
    int unit_test__storage__copy_string();
    int unit_test__monotonic_clock_increases_monotonically();
    int unit_test__clock_sleep_ms_accepts_null();
}

//
//      TEST DRIVER
//

int
main()
{
    struct testcase
    {
        const char* name;
        int (*test)();
    };
    const std::vector<testcase> tests{
#define CASE(e) { .name = #e, .test = (e) }
        CASE(unit_test__device_state_as_string__is_defined_for_all),
        CASE(unit_test__device_kind_as_string__is_defined_for_all),
        CASE(unit_test__storage__storage_property_string_check),
        CASE(unit_test__storage__copy_string),
        CASE(unit_test__monotonic_clock_increases_monotonically),
        CASE(unit_test__clock_sleep_ms_accepts_null),
#undef CASE
    };

    // test cases that instantiate their own runtimes
    struct contained_testcase
    {
        const char* name;
        int (*test)(reporter_t);
    };
    const std::vector<contained_testcase> contained_tests{
#define CASE(e)                                                                \
    {                                                                          \
        .name = #e, .test = (e)                                                \
    }
    // CASE(unit_test__monitor_uninitialized_on_stop),
#undef CASE
    };

    bool any = false;

    struct AcquireRuntime* runtime = acquire_init(reporter);
    for (const auto& test : tests) {
        LOG("Running %s", test.name);
        if (!(test.test())) {
            ERR("unit test failed: %s", test.name);
            any = true;
        }
    }
    acquire_shutdown(runtime);

    for (const auto& test : contained_tests) {
        LOG("Running %s", test.name);
        if (!(test.test(reporter))) {
            ERR("unit test failed: %s", test.name);
            any = true;
        }
    }
    return any;
}

// TODO: Figure out what to do about unit tests.
// The test driver is in the root test directory. This function gets declared
// as extern there. Other unit tests should end up getting aggregating there.
// Maybe we use a NO_UNIT_TESTS define so the tests get excluded from a
// production build...
