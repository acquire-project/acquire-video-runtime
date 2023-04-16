#include "acquire.h"
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

int
main()
{
    auto runtime = acquire_init(reporter);
    CHECK(runtime);
    // Expect zero-config start to fail predictably.
    // Should return an error code.
    CHECK(AcquireStatus_Error == acquire_start(runtime));
    CHECK(AcquireStatus_Ok == acquire_shutdown(runtime));
    return 0;
}
