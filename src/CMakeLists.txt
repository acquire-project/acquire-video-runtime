add_subdirectory(acquire-device-hal)

set(tgt acquire-video-runtime)
add_library(${tgt} STATIC
        acquire.h
        acquire.c
        runtime/channel.h
        runtime/channel.c
        runtime/throttler.h
        runtime/throttler.c
        runtime/video.h
        runtime/source.h
        runtime/source.c
        runtime/filter.h
        runtime/filter.c
        runtime/sink.h
        runtime/sink.c
        runtime/vfslice.h
        runtime/vfslice.c
        runtime/frame_iterator.c
        runtime/frame_iterator.h
)
target_enable_simd(${tgt})
target_link_libraries(${tgt} PUBLIC
        acquire-core-logger
        acquire-core-platform
        acquire-device-hal
        acquire-device-kit
        acquire-device-properties
)
target_include_directories(${tgt}
        PUBLIC ${CMAKE_CURRENT_LIST_DIR}
)
set_target_properties(${tgt} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        PUBLIC_HEADER "acquire.h"
)
