if(NOT TARGET acquire-core-logger)
        add_subdirectory(acquire-core-libs)
endif()

set(tgt acquire-video-runtime)
add_library(${tgt} STATIC
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
target_sources(${tgt} PUBLIC FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}
        FILES
        acquire.h
)
target_enable_simd(${tgt})
target_link_libraries(${tgt} PUBLIC
        acquire-core-logger
        acquire-core-platform
        acquire-device-properties
        acquire-device-kit
        acquire-device-hal
)
target_include_directories(${tgt}
        PUBLIC ${CMAKE_CURRENT_LIST_DIR}
)
target_add_git_versioning(${tgt})

install(TARGETS ${tgt} FILE_SET HEADERS)
