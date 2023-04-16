//!
//! # VideoFrame iterator
//!
//! VideoFrames may be varying in size. Each is a header with some size data
//! followed by a buffer containing pixel data. We're often working with a
//! collection of VideoFrames in a contiguous bit of memory. The raw memory
//! range is represented by a `slice` (also see `vfslice`).
//!
//! The `frame_iterator` helps address each successive `VideoFrame` in a
//! contiguous series of `VideoFrame`s.
//!
//! Example:
//!
//! ~~~{.c}
//!     struct frame_iterator it=frame_iterator_init(slice);
//!     struct VideoFrame* frame=0;
//!     while((frame=frame_iterator_next(&it))) {...}
//! ~~~
//!

#ifndef H_ACQUIRE_FRAME_ITERATOR_V0
#define H_ACQUIRE_FRAME_ITERATOR_V0

#include "channel.h"

#ifdef __cplusplus
extern "C"
{
#endif

    struct frame_iterator
    {
        struct slice remaining;
    };

    struct frame_iterator frame_iterator_init(struct slice* slice);

    struct VideoFrame* frame_iterator_next(struct frame_iterator* it);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // H_ACQUIRE_FRAME_ITERATOR_V0
