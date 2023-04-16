if(${NOTEST})
    message(STATUS "Skipping test targets")
else()
    foreach(tgt
        abort-or-stop
        change-external-metadata
        change-file-name
        client-queue-is-flushed-after-abort
        configure-after-shutdown-and-reinitialize
        device-selection
        instance-stage-axis
        list-devices
        no-abort-on-dropped-frames
        one-video-stream
        repeat-start
        repeat-start-no-stop
        two-video-streams
        unit-tests
        zero-config-start
    )
        add_executable(${tgt} ${tgt}.cpp)
        target_link_libraries(${tgt}
            acquire-video-runtime
            acquire-device-kit
            acquire-device-hal
        )
        target_compile_definitions(${tgt} PUBLIC TEST="${tgt}")
        set_target_properties(${tgt} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        add_test(NAME test-${tgt} COMMAND ${tgt})
        set_tests_properties(test-${tgt} PROPERTIES LABELS anyplatform)

        # target_add_simulated_cameras_driver(${tgt})
        # target_add_core_video_sinks(${tgt})
    endforeach()
endif()
