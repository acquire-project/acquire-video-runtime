#ifndef H_ACQUIRE_V0
#define H_ACQUIRE_V0

#include "device/props/device.h"
#include "device/props/camera.h"
#include "device/props/storage.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum AcquireStatusCode
    {
        AcquireStatus_Ok = 0,
        AcquireStatus_Error,
    };

    struct AcquireRuntime
    {
        void* impl;
    };

    struct AcquireProperties
    {
        struct aq_properties_video_s
        {
            struct aq_properties_camera_s
            {
                struct DeviceIdentifier identifier;
                struct CameraProperties settings;
            } camera;
            struct aq_properties_storage_s
            {
                struct DeviceIdentifier identifier;
                struct StorageProperties settings;
                float write_delay_ms;
            } storage;
            uint64_t max_frame_count;
            uint32_t frame_average_count;
        } video[2];
    };

    struct AcquirePropertyMetadata
    {
        struct aq_metadata_video_s
        {
            struct CameraPropertyMetadata camera;
            struct StoragePropertyMetadata storage;
            //  description
            struct Property max_frame_count;
            struct Property frame_average_count;
        } video[2];
    };

    const char* acquire_api_version_string();

    /// Creates and initializes the `AcquireRuntime`.
    ///
    /// This is the starting point for working with the 'acquire' api.
    ///
    /// Caller is responsible for calling `acquire_shutdown()` when done.  Most
    /// `acquire` functions are asynchronous (they don't block).
    /// `acquire_shutdown()` will block, waiting for outstanding work to
    /// complete, and release resources.
    ///
    /// This function returns the runtime in a `AwaitingConfiguration` state.
    /// Use `acquire_configure()` and `acquire_set_configuration()` to query and
    /// manipulate configuration.
    ///
    /// @param[in] reporter A callback function invoked when there is some
    ///                     logging output.
    /// @return runtime     A pointer to the runtime object. Allocated on
    ///                     the heap. Freed by `acquire_shutdown()`
    struct AcquireRuntime* acquire_init(void (*reporter)(int is_error,
                                                         const char* file,
                                                         int line,
                                                         const char* function,
                                                         const char* msg));

    enum AcquireStatusCode acquire_shutdown(struct AcquireRuntime* self);

    enum AcquireStatusCode acquire_configure(
      struct AcquireRuntime* self,
      struct AcquireProperties* settings);

    /// @param[in]  self        AcquireRuntime. The runtime context to query.
    /// @param[out] properties  Must not be NULL. Populated with the result.
    enum AcquireStatusCode acquire_get_configuration(
      const struct AcquireRuntime* self,
      struct AcquireProperties* properties);

    enum AcquireStatusCode acquire_get_configuration_metadata(
      const struct AcquireRuntime* self,
      struct AcquirePropertyMetadata* metadata);

    /// @param[in] self `AcquireRuntime*` The runtime instance.
    /// @returns a pointer that references the device manager for `self`
    const struct DeviceManager* acquire_device_manager(
      const struct AcquireRuntime* self);

    enum AcquireStatusCode acquire_get_shape(const struct AcquireRuntime* self,
                                             uint32_t istream,
                                             struct ImageShape* shape);

    enum AcquireStatusCode acquire_start(struct AcquireRuntime* self);

    enum AcquireStatusCode acquire_stop(struct AcquireRuntime* self);

    enum AcquireStatusCode acquire_abort(struct AcquireRuntime* self);

    enum AcquireStatusCode acquire_execute_trigger(struct AcquireRuntime* self,
                                                   uint32_t istream);

    enum DeviceState acquire_get_state(struct AcquireRuntime* self);

    /// @brief Read's data from a video stream.
    /// @see acquire_map_unread()
    /// @param[in] self 'runtime' reference.
    /// @param[in] stream Integer index selecting the video output stream to
    ///                   read.
    /// @param[out] beg Must be non-NULL. Populated with the starting address
    ///                 the memory region mapped for reading.
    /// @param[out] end Must be non-NULL. Populated with the ending address of
    ///                 the memory region mapped for reading.
    /// @returns AcquireStatus_Error if there was an error, otherwise
    /// AcquireStatus_Ok. Usually an error corresponds to an invalid parameter.
    //
    /// Reserves a region of the video `istream`'th stream for reading. The
    /// output interval `[beg,end)` will remain valid for reading until unmapped
    /// by `acquire_unmap_read()`.
    ///
    /// Each call to `acquire_map_read()` returns the next unread interval of
    /// data. When no new data is available an empty region is returned
    /// (`*beg==*end`) - this call does not wait for data.
    ///
    /// Holding on to a mapped region will prevent writers from making progress.
    /// Call `acquire_unmap_read()` to release.
    enum AcquireStatusCode acquire_map_read(const struct AcquireRuntime* self,
                                            uint32_t istream,
                                            struct VideoFrame** beg,
                                            struct VideoFrame** end);

    /// @brief Releases the read region reserved for the `istream`'th video
    /// stream.
    /// @see acquire_map_read()
    /// @param[in] self 'runtime' reference.
    /// @param[in] stream Integer index selecting the video output stream to
    ///                   read.
    /// @param[in] consumed_bytes
    enum AcquireStatusCode acquire_unmap_read(const struct AcquireRuntime* self,
                                              uint32_t istream,
                                              size_t consumed_bytes);

    size_t acquire_bytes_waiting_to_be_written_to_disk(
      const struct AcquireRuntime* self,
      uint32_t istream);

#ifdef __cplusplus
}
#endif

#endif // H_ACQUIRE_V0
