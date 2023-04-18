if(${NOTEST})
    message(STATUS "Skipping test targets")
else()
    aq_require(acquire-driver-common)

    #
    # PARAMETERS
    #
    set(project acquire-driver-runtime) # CMAKE_PROJECT_NAME gets overridden if this is a subtree of another project
    set(driver acquire-driver-common)

    #
    # Copy driver to tests
    #
    add_custom_target(${project}-copy-${driver}-for-tests
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${driver}>
        ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${driver}
        COMMENT "Copying ${driver} to ${CMAKE_CURRENT_BINARY_DIR}"
    )

    #
    # Tests
    #
    foreach(name
        abort-or-stop
        change-external-metadata
        change-file-name
        client-queue-is-flushed-after-abort
        configure-after-shutdown-and-reinitialize
        device-selection
        list-devices
        no-abort-on-dropped-frames
        one-video-stream
        repeat-start
        repeat-start-no-stop
        two-video-streams
        unit-tests
        zero-config-start
    )
        set(tgt "${project}-${name}")
        add_executable(${tgt} ${name}.cpp)
        target_link_libraries(${tgt}
            acquire-video-runtime
            acquire-device-kit
            acquire-device-hal
        )
        target_compile_definitions(${tgt} PUBLIC TEST="${tgt}")
        add_dependencies(${tgt} ${project}-copy-${driver}-for-tests)
        set_target_properties(${tgt} PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        add_test(NAME test-${tgt} COMMAND ${tgt})
        set_tests_properties(test-${tgt} PROPERTIES LABELS anyplatform)
    endforeach()
endif()
