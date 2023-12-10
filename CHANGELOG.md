# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.2](https://github.com/acquire-project/acquire-video-runtime/compare/v0.1.1...v0.1.2) - 2023-06-27

### Changed

- After the storage thread starts, the runtime will signal the storage device to expect an image shape from the camera.
- Reflect a change in core-libs: `StorageProperties::chunking::max_bytes_per_chunk` is now a `uint64_t` (was
  a `uint32_t`).

### Added

- Nightly releases.
- The runtime will also get the storage property metadata when it gets the configuration metadata.
- Can optionally link against the DLL version of the MSVC runtime.

### Fixed

- Check that the monitor's read region is fully flushed before stopping the runtime.

## 0.1.0 - 2023-05-11
