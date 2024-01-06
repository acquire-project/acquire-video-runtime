#ifndef H_ACQUIRE_SINK_V0
#define H_ACQUIRE_SINK_V0

#include "platform.h"
#include "channel.h"
#include "device/props/device.h"
#include "device/props/storage.h"
#include "device/hal/storage.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Context for video sink threads
    struct video_sink_s
    {
        /// Used by external threads to signal the controller thread to stop
        /// Other threads may write.
        uint8_t is_stopping;

        /// When true, the controller thread has completed it's work.
        /// Other threads should only read.
        uint8_t is_running;

        uint8_t stream_id;
        float write_delay_ms;
        void (*sig_stop_source)(const struct video_sink_s*);
        struct Storage* storage;
        struct channel in;
        struct thread thread;
        struct DeviceIdentifier identifier;
        struct channel_reader reader;
    };

    enum DeviceStatusCode video_sink_init(
      struct video_sink_s* self,
      uint8_t stream_id,
      size_t channel_capacity_bytes,
      void (*sig_stop_source)(const struct video_sink_s*));

    void video_sink_destroy(struct video_sink_s* self);

    enum DeviceStatusCode video_sink_start(struct video_sink_s* self);

    /// @brief Query the video sink controller's properties.
    /// @param [in] self A `video_sink_s` context.
    /// @param [out] identifier The`DeviceIdentifier` of the current video sink
    /// device.
    /// @param [out] settings The current `StorageProperties`.
    /// @param [out] write_delay_ms The current write delay.
    /// @return Device_Ok on success, otherwise Device_Err
    enum DeviceStatusCode video_sink_get(const struct video_sink_s* self,
                                         struct DeviceIdentifier* identifier,
                                         struct StorageProperties* settings,
                                         float* write_delay_ms);

    enum DeviceStatusCode video_sink_configure(
      struct video_sink_s* self,
      const struct DeviceManager* device_manager,
      struct DeviceIdentifier* identifier,
      struct StorageProperties* settings,
      float write_delay_ms);

    size_t video_sink_bytes_waiting(const struct video_sink_s* self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // H_ACQUIRE_SINK_V0
