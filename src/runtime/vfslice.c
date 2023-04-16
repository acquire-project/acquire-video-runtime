
#include "vfslice.h"

#define offsetby(T, P, B) ((T*)((uint8_t*)(P) + (B)))

struct vfslice
make_vfslice(const struct slice slice)
{
    return (struct vfslice){ .beg = (const struct VideoFrame*)slice.beg,
                             .end = (const struct VideoFrame*)slice.end };
}

struct vfslice_mut
make_vfslice_mut(const struct slice slice)
{
    return (struct vfslice_mut){ .beg = (struct VideoFrame*)slice.beg,
                                 .end = (struct VideoFrame*)slice.end };
}

/// @brief consumes frames from `[beg,end)` while frames are older than
/// `delay_ms`.
/// @returns A `VideoFrame*` slice with the unconsumed data
struct vfslice
vfslice_split_at_delay_ms(const struct vfslice* slice, float delay_ms)
{
    if (slice->beg >= slice->end) {
        return *slice;
    }

    if (delay_ms < 1.0e-3f) {
        return (struct vfslice){ .beg = slice->end, .end = slice->end };
    }

    struct clock now = { 0 };
    clock_init(&now);
    clock_shift_ms(&now, -delay_ms);

    const struct VideoFrame* cur;
    for (cur = slice->beg; cur < slice->end;
         cur = offsetby(const struct VideoFrame, cur, cur->bytes_of_frame)) {
        if (clock_cmp(&now, cur->timestamps.acq_thread) > 0)
            break;
    }
    return (struct vfslice){ .beg = cur, .end = slice->end };
}
