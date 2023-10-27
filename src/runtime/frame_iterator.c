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
    if (it->remaining.beg == 0 || it->remaining.beg >= it->remaining.end) {
        it->remaining = (struct slice){ 0 };
        return 0;
    }
    struct VideoFrame* cur = (struct VideoFrame*)it->remaining.beg;
    it->remaining.beg += cur->bytes_of_frame;
    return cur;
}
