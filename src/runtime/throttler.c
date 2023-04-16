#include "throttler.h"

struct throttler
throttler_init(float seconds_per_loop)
{
    struct throttler out = { .milliseconds = 1e3f * seconds_per_loop };
    clock_init(&out.clock);
    return out;
}

void
throttler_wait(struct throttler* self)
{
    clock_sleep_ms(&self->clock, self->milliseconds);
}
