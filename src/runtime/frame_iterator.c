#include "frame_iterator.h"
#include "device/props/components.h"

struct frame_iterator
frame_iterator_init(struct slice* slice)
{
    return (struct frame_iterator){ .remaining = *slice };
}

struct VideoFrame*
frame_iterator_next(struct frame_iterator* it)
{
    struct VideoFrame* cur = (struct VideoFrame*)it->remaining.beg;
    if (cur) {
        const uint8_t* next = it->remaining.beg + cur->bytes_of_frame;
        if (next < it->remaining.end) {
            it->remaining.beg += cur->bytes_of_frame;
        } else {
            cur = 0;
            it->remaining = (struct slice){ 0 };
        }
    }
    return cur;
}
