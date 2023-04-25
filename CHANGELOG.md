# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0](https://github.com/acquire-project/acquire-python/compare/v0.1.0...v0.2.0) - 2023-03-29

### Added

- Blosc compression is now supported on macOS in Zarr.
- Zarr datasets are now written according to v0.4 of
  the [OME-NGFF spec](https://ngff.openmicroscopy.org/0.4/index.html).
- New device manager API function, `device_manager_select_first`, to select the first device of a given `DeviceKind` without supplying a
  name.
- Zarr now supports LZ4 compression.
- Tiff now supports a hardware frame ID in its per-frame metadata.
- Tiff now supports a pixel scaling factor in metadata.
- Zarr chunk file size can be configured.
- Euresys eGrabber camera driver support.

### Changed

- The runtime will now warn you if one or more frames are dropped during acquisition.
- Querying the status of the runtime now checks whether any streaming, frame processing, or storage threads are still
  running and updates the runtime's status accordingly.
- The device manager is no longer a singleton.
- The DCam driver will attempt to reset itself if a camera fails to start acquiring.

### Fixed

- The demo app now uses `acquire_abort` instead of `acquire_stop` to terminate acquisition, no longer hanging indefinitely.
- The runtime checks whether the monitor reader has been initialized before attempting to clear it.
- We can reinitialize and configure a new runtime after `acquire_shutdown` without segfaulting.

## [0.1.0](https://github.com/acquire-project/acquire-python/compare/v0.0.0...v0.1.0) - 2023-01-18

### Added

- Two-camera support.
- Support for `line_interval_us` and `readout_direction` properties in DCam plugin.
- Support for external metadata.
- Support for writing Tiff with external metadata stored as JSON.
- Support for basic Zarr writing, with chunking along append dimension only.
- New API function, `acquire_abort`, terminates acquisition immediately.
- Linux support.
- Windows and Linux support for Zstd compression in Zarr using the Blosc metacompressor.

### Changed

- API function `acquire_stop` now waits until the maximum number of frames is acquired before stopping the runtime.

### Fixed

- DCam triggering configuration fixes.
- Fixed wrong ordering of simulated cameras.
- Fixed pipeline monitor map/unmap bug, causing hangs in some situations.

## [0.0.0] - 2022-09-08
