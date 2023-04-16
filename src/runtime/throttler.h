//! Utility for ensuring each iteration of a loop takes at least `milliseconds`.
//!
//! Example:
//!
//!     struct throttler throttler=throttler_init(0.1); // 100 ms
//!     while(1) {
//!         throttler_wait(&throttler);
//!     }
#ifndef H_ACQUIRE_THROTTLER_V0
#define H_ACQUIRE_THROTTLER_V0

#include "platform.h"

struct throttler
{
    struct clock clock;
    float milliseconds;
};

struct throttler
throttler_init(float seconds_per_loop);

void
throttler_wait(struct throttler* self);

#endif // H_ACQUIRE_THROTTLER_V0
