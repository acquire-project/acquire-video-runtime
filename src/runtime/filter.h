#ifndef H_ACQUIRE_FILTER_V0
#define H_ACQUIRE_FILTER_V0

#include <stdint.h>
#include "channel.h"
#include "device/props/device.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Context for video filter threads
    struct video_filter_s
    {
        uint32_t filter_window_frames;
        struct channel in;
        struct channel* out;
        struct channel_reader reader;
        int sig_accumulator_reset;

        /// Used by external threads to signal the controller thread to stop
        /// Other threads may write.
        uint8_t is_stopping;

        /// When true, the controller thread has completed it's work.
        /// Other threads should only read.
        uint8_t is_running;

        struct event accumulator_reset_event;
        struct thread thread;
        uint8_t stream_id;
    };

    enum DeviceStatusCode video_filter_init(struct video_filter_s* self,
                                            uint8_t stream_id,
                                            size_t channel_size_bytes,
                                            struct channel* out);

    void video_filter_destroy(struct video_filter_s* self);

    enum DeviceStatusCode video_filter_configure(struct video_filter_s* self,
                                                 uint32_t frame_average_count);

    enum DeviceStatusCode video_filter_start(struct video_filter_s* self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // H_ACQUIRE_FILTER_V0
