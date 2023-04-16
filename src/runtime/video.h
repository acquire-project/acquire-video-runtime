#ifndef H_ACQUIRE_VIDEO_V0
#define H_ACQUIRE_VIDEO_V0

#include <stdint.h>
#include "channel.h"
#include "sink.h"
#include "source.h"
#include "filter.h"

#ifdef __cplusplus
extern "C"
{
#endif
    struct video_monitor_s
    {
        struct channel_reader reader;
    };

    struct video_s
    {
        /// The index of this in the `videos[]` array.
        /// Set in `acquire_init()`
        uint8_t stream_id;
        struct video_monitor_s
          monitor; //< A reader exposed through the public api

        struct video_source_s source; //< context for the video source thread
        struct video_filter_s filter; //< context for the video filter thread
        struct video_sink_s sink;     //< context for the video sink thread
    };

#ifdef __cplusplus
} // extern "C"
#endif

#endif // H_ACQUIRE_VIDEO_V0
