//! VideoFrame slice objects
//!
//! VideoFrames may be varying in size. Each is a header with some size data
//! followed by a buffer containing pixel data. We're often working with a
//! collection of VideoFrames in a contiguous bit of memory. The memory range
//! is represented by a `vfslice`.

#ifndef H_ACQUIRE_VFSLICE_V0
#define H_ACQUIRE_VFSLICE_V0

#include "runtime/channel.h"
#include "device/props/components.h"

#ifdef __cplusplus
extern "C"
{
#endif

    struct vfslice
    {
        const struct VideoFrame *beg, *end;
    };

    struct vfslice_mut
    {
        struct VideoFrame *beg, *end;
    };

    struct vfslice make_vfslice(const struct slice slice);

    struct vfslice_mut make_vfslice_mut(const struct slice slice);

    struct vfslice vfslice_split_at_delay_ms(const struct vfslice* slice,
                                             float delay_ms);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // H_ACQUIRE_VFSLICE_V0
