#include "channel.h"
#include <string.h>

#define countof(e) (sizeof(e) / sizeof((e)[0]))
#define MAX_READERS countof(((struct channel*)0)->holds.pos) //(1 << 3)

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

static int
cursor_cmp(size_t cycle_a, size_t pos_a, size_t cycle_b, size_t pos_b)
{
    if (cycle_a < cycle_b)
        return -1;
    if (cycle_a > cycle_b)
        return 1;
    if (pos_a < pos_b)
        return -1;
    if (pos_a > pos_b)
        return 1;
    return 0;
}

static uint32_t
reader_min(const size_t* tails, const size_t* cycles, uint32_t n)
{
    struct
    {
        size_t tail, cycle;
    } mn = {
        .tail = tails[0],
        .cycle = cycles[0],
    };
    uint32_t argmin = 0;
    for (uint32_t i = 1; i < n; ++i) {
        if (cursor_cmp(mn.cycle, mn.tail, cycles[i], tails[i]) == 1) {
            mn.tail = tails[i];
            mn.cycle = cycles[i];
            argmin = i;
        }
    }
    return argmin;
}

static uint32_t
next_write(const struct channel* self,
           size_t nbytes,
           size_t* beg,
           uint8_t* should_wrap)
{
    *should_wrap = 0;

    if (!self->is_accepting_writes)
        return 0;

    const uint32_t argmin =
      reader_min(self->holds.pos, self->holds.cycles, self->holds.n);
    const size_t tail = self->holds.pos[argmin];

    if (self->head < tail) {
        *beg = self->head;
        return nbytes <= (tail - self->head);
    }

    if (tail == self->head && (self->cycle == self->holds.cycles[argmin] + 1)) {
        return 0;
    }

    if (nbytes <= (self->capacity - self->head)) {
        *beg = self->head;
        return 1;
    }

    if (nbytes <= tail) {
        *beg = 0;
        return 1;
    }

    if (tail == self->head) {
        *beg = 0;
        *should_wrap = 1;
        return nbytes < self->capacity;
    }

    return 0;
}

static int
reader_initialize(struct channel* self, struct channel_reader* reader)
{
    if (reader->id > 0)
        return 1;
    reader->id = ++self->holds.n;
    self->holds.cycles[reader->id - 1] = self->cycle;
    self->holds.pos[reader->id - 1] = 0;
    if (self->holds.n >= MAX_READERS)
        return 0;
    return 1;
}

static size_t
get_available_byte_count(const struct channel_reader* const reader,
                         const size_t pos,
                         const size_t cycle,
                         const size_t high)
{
    if (reader->pos == pos && reader->cycle == cycle)
        return 0;
    if (reader->pos == 0)
        return high - pos;
    return reader->pos - pos;
}

void
channel_new(struct channel* self, size_t capacity)
{
    *self = (struct channel){
        .data = memory_alloc(capacity, AllocatorHint_LargePage),
        .capacity = capacity,
    };

    lock_init(&self->lock);
    condition_variable_init(&self->notify_space_available);
    memset(self->data, 0, capacity); // NOLINT
    self->is_accepting_writes = 1;
}

void
channel_release(struct channel* self)
{
    self->holds.n = 0;
    condition_variable_notify_all(&self->notify_space_available);

    lock_acquire(&self->lock);
    memory_free(self->data);
    self->capacity = 0;
    self->head = 0;
    lock_release(&self->lock);
}

void
channel_accept_writes(struct channel* self, uint32_t tf)
{
    self->is_accepting_writes = tf;
    condition_variable_notify_all(&self->notify_space_available);
}

void
channel_abort_write(struct channel* self)
{
    lock_acquire(&self->lock);
    if (self->is_accepting_writes) {
        self->mapped = self->head;
    }
    lock_release(&self->lock);
}

struct slice
channel_read_map(struct channel* self, struct channel_reader* reader)
{
    size_t nbytes = 0;
    lock_acquire(&self->lock);

    reader_initialize(self, reader);

    size_t* const cycle = self->holds.cycles + reader->id - 1;
    size_t* const pos = self->holds.pos + reader->id - 1;
    uint8_t* out = self->data + *pos;

    if (reader->state == ChannelState_Mapped) {
        reader->status = Channel_Expected_Unmapped_Reader;
        goto Error;
    }

    if (*pos == self->head && *cycle == self->cycle) {
        goto Finalize;
    }

    if (*pos < self->head) {
        if (*cycle != self->cycle)
            goto Overflow;
        nbytes = self->head - *pos;
        reader->pos = self->head;
        reader->cycle = self->cycle;
    } else {
        if (self->cycle != *cycle + 1)
            goto Overflow;
        nbytes = self->high - *pos;
        reader->pos = 0;
        reader->cycle = *cycle + 1;
    }

    reader->state = ChannelState_Mapped;

Finalize:
    lock_release(&self->lock);
    return (struct slice){ .beg = out, .end = out + nbytes };
Overflow:
    reader->status = Channel_Error;
Error:
    out = 0;
    nbytes = 0;
    *pos = self->head;
    *cycle = self->cycle;
    goto Finalize;
}

void
channel_read_unmap(struct channel* self,
                   struct channel_reader* reader,
                   size_t consumed_bytes)
{
    if (reader->state != ChannelState_Mapped)
        return;
    lock_acquire(&self->lock);

    size_t* const cycle = self->holds.cycles + reader->id - 1;
    size_t* const pos = self->holds.pos + reader->id - 1;

    size_t length = get_available_byte_count(reader, *pos, *cycle, self->high);
    consumed_bytes = min(length, consumed_bytes);
    if (consumed_bytes >= length) {
        *cycle = reader->cycle;
        *pos = reader->pos;
    } else {
        *pos += consumed_bytes;
    }
    if (self->head < *pos && *pos == self->high) {
        *pos = 0;
        *cycle += 1;
    }
    reader->state = ChannelState_Unmapped;
    lock_release(&self->lock);
    condition_variable_notify_all(&self->notify_space_available);
}

void*
channel_write_map(struct channel* self, size_t nbytes)
{
    void* out = 0;
    if (nbytes >= self->capacity)
        return 0;
    lock_acquire(&self->lock);

    size_t beg, end;
    if (!self->holds.n) {
        beg = self->head;
        end = self->head + nbytes;
        if (end >= self->capacity) {
            self->high = self->head;
            ++self->cycle;
            self->head = beg = 0;
            end = nbytes;
        }
    } else {
        uint8_t should_wrap = 0;

        while (self->is_accepting_writes &&
               !next_write(self, nbytes, &beg, &should_wrap)) {

            condition_variable_wait(&self->notify_space_available, &self->lock);
        }
        if (!self->is_accepting_writes)
            goto Finalize;
        end = beg + nbytes;
        if (beg != self->head) {
            self->high = self->head;
            self->head = beg;
            ++self->cycle;
        }
        if (should_wrap) {
            for (uint32_t i = 0; i < self->holds.n; ++i) {
                self->holds.pos[i] = 0;
                self->holds.cycles[i] = self->cycle;
            }
        }
    }
    out = self->data + beg;
    self->mapped = end;
Finalize:
    lock_release(&self->lock);
    return out;
}

void
channel_write_unmap(struct channel* self)
{
    lock_acquire(&self->lock);
    if (self->is_accepting_writes) {
        self->head = self->mapped;
    }
    lock_release(&self->lock);
}
