#ifndef H_ACQUIRE_RUNTIME_VIDEO_SOURCE_V0
#define H_ACQUIRE_RUNTIME_VIDEO_SOURCE_V0

#include "device/props/device.h"
#include "device/props/camera.h"
#include "device/hal/device.manager.h"
#include "platform.h"
#include "runtime/channel.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Context for video source threads
    struct video_source_s
    {
        struct Camera* camera;
        struct DeviceIdentifier last_camera_id;
        uint64_t max_frame_count;

        /// Used by external threads to signal the controller thread to stop
        /// Other threads may write.
        uint8_t is_stopping;

        /// When true, the controller thread has completed it's work.
        /// Other threads should only read.
        uint8_t is_running;

        uint8_t stream_id;
        struct thread thread;
        struct channel* to_sink;
        struct channel* to_filter;
        uint8_t enable_filter;

        /// Signals stream filters to reset any internal state and blocks until
        /// the reset is completed.
        void (*await_filter_reset)(const struct video_source_s*);

        void (*sig_stop_filter)(const struct video_source_s*);
        void (*sig_stop_sink)(const struct video_source_s*);
    };

    /// @brief Initializes the video source controller.
    /// @param[out] self The video source controller. Memory is zeroed.
    /// @param[in] stream_id The associated stream id
    /// @param[in] max_frame_count Number of frames to acquire in a finite
    /// acquisition. Set to -1 for infinite.
    /// @param[in] to_sink video frame channel consumed by the sink.
    /// @param[in] to_filter video frame channel consumed by any filtering.
    /// @return `Device_Ok` on success, otherwise `Device_Err`
    ///
    /// The video source may output to either the `to_sink` channel or the
    /// `to_filter` channel on any given frame depending on whether or not the
    /// filter stream is enabled. Initially filtering is disabled.
    enum DeviceStatusCode video_source_init(
      struct video_source_s* self,
      uint8_t stream_id,
      uint64_t max_frame_count,
      struct channel* to_sink,
      struct channel* to_filter,
      void (*await_filter_reset)(const struct video_source_s*),
      void (*sig_stop_filter)(const struct video_source_s*),
      void (*sig_stop_sink)(const struct video_source_s*));

    void video_source_destroy(struct video_source_s* self);

    /// @brief Query the video source controller's properties.
    /// @param[in] self video source context
    /// @param[out] source_device_identifier The `DeviceIdentifier` of the last
    /// source device.
    /// @param[out] settings The current device properties. Only updated if
    /// a device is open.
    /// @param[out] max_frame_count The number of frames to acquire for a finite
    /// acquisition.
    /// @return `Device_Ok` on success, otherwise `Device_Err`.
    enum DeviceStatusCode video_source_get(
      const struct video_source_s* self,
      struct DeviceIdentifier* source_device_identifier,
      struct CameraProperties* settings,
      uint64_t* max_frame_count);

    enum DeviceStatusCode video_source_configure(
      struct video_source_s* self,
      const struct DeviceManager* device_manager,
      struct DeviceIdentifier* identifier,
      struct CameraProperties* settings,
      uint64_t max_frame_count,
      uint8_t enable_filter);

    enum DeviceStatusCode video_source_start(struct video_source_s* self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // H_ACQUIRE_RUNTIME_VIDEO_SOURCE_V0
