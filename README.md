# Acquire Video

[![Tests](https://github.com/acquire-project/acquire-video-runtime/actions/workflows/test_pr.yml/badge.svg)](https://github.com/acquire-project/acquire-video-runtime/actions/workflows/test_pr.yml)
[![Chat](https://img.shields.io/badge/zulip-join_chat-brightgreen.svg)](https://acquire-imaging.zulipchat.com/)

This is a multi-camera video streaming library _focusing_ on cameras and file-formats for microscopy.

This is the video runtime for the Acquire project.

## Platform support

The library supports Windows, OSX and Linux.

Windows support is the highest priority and has the widest device compatibility. OSX and Linux are actively tested.

## Compiler support

The primary compiler targeted is MSVC's toolchain.

## Build environment

1. Install CMake 3.23 or newer
2. Install a fairly recent version of Microsoft Visual Studio.

From the repository root:

```
mkdir build
cd build
cmake-gui ..
```

## Build

From the repository root:

```
cmake --build build
```

## Using [pre-commit](https://pre-commit.com/)

Pre-commit is a utility that runs certain checks before you can create a commit
in git. This helps ensure each commit addresses any quality/formatting issues.

To use, first install it. It's a python package so you can use `pip`.

```
pip install pre-commit
```

Then, navigate to this repo and initialize:

```
cd acquire
pre-commit install
```

This will configure the git hooks so they get run the next time you try to commit.

> **Tips**
>
> - The formatting checks modify the files to fix them.
> - The commands that get run are in `.pre-commit-config.yaml`
    > You'll have to install those.
> - `cppcheck` is disabled by default, but can be enabled by uncommenting the
    > corresponding lines in `.pre-commit-config.yaml`. See that file for more
    > details.
