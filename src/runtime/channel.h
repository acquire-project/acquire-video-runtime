#ifndef H_ACQUIRE_CHANNEL_V0
#define H_ACQUIRE_CHANNEL_V0

#include "platform.h"

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    /// @brief A bipartite circular queue for zero-copy streaming to multiple
    /// consumers.
    ///
    /// Inspired by
    /// https://www.codeproject.com/Articles/3479/The-Bip-Buffer-The-Circular-Buffer-with-a-Twist
    struct channel
    {
        struct lock lock;
        struct condition_variable notify_space_available;

        /// Pointer to the start of the channel's buffer.
        uint8_t* data;

        /// Maximum number of bytes this channel can hold.
        size_t capacity;

        /// Position in the channel where the next write operation will occur.
        size_t head;

        /// Highest position in the channel that has been written to in the current cycle.
        size_t high;

        /// Number of times the buffer has been filled and wrapped around to the start.
        size_t cycle;

        /// Pointer to the end position of the reserved region of a mapped write.
        size_t mapped;

        /// Whether or not the channel is accepting writes.
        unsigned char is_accepting_writes;

        /// Current positions and cycles of readers on this channel.
        struct
        {
            size_t pos[8];
            size_t cycles[8];
            unsigned n; /// Number of readers currently reading from the channel.
        } holds;
    };

    struct slice
    {
        uint8_t *beg, *end;
    };

    enum ChannelStatus
    {
        Channel_Ok = 0,
        Channel_Error,
        Channel_Expected_Unmapped_Reader
    };

    enum ChannelState
    {
        ChannelState_Unmapped = 0,
        ChannelState_Mapped,
    };

    struct channel_reader
    {
        unsigned id;
        size_t pos, cycle;
        enum ChannelStatus status;
        enum ChannelState state;
    };

    void channel_new(struct channel* self, size_t capacity);

    void channel_release(struct channel* self);

    void* channel_write_map(struct channel* self, size_t nbytes);

    void channel_write_unmap(struct channel* self);

    void channel_abort_write(struct channel* self);

    void channel_accept_writes(struct channel* self, uint32_t tf);

    struct slice channel_read_map(struct channel* self,
                                  struct channel_reader* reader);

    void channel_read_unmap(struct channel* self,
                            struct channel_reader* reader,
                            size_t consumed_bytes);

#ifdef __cplusplus
} // end extern "C"
#endif //__cplusplus

#endif // H_ACQUIRE_CHANNEL_V0
