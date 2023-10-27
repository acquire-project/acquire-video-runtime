if(${NOTEST})
    message(STATUS "Skipping test targets")
else()
    set(NOTEST "TRUE")
    aq_require(acquire-driver-common)
    set(NOTEST "FALSE")

    #
    # PARAMETERS
    #
    set(project acquire-driver-runtime) # CMAKE_PROJECT_NAME gets overridden if this is a subtree of another project
    set(driver acquire-driver-common)

    #
    # Tests
    #
    set(tests
        list-devices
        abort-or-stop
        change-external-metadata
        change-file-name
        client-queue-is-flushed-after-abort
        configure-after-shutdown-and-reinitialize
        device-selection
        no-abort-on-dropped-frames
        one-video-stream
        repeat-start
        repeat-start-no-stop
        two-video-streams
        unit-tests
        zero-config-start
        write-side-by-side-tiff
        filter-video-average
    )

    foreach(name ${tests})
        set(tgt "${project}-${name}")
        add_executable(${tgt} ${name}.cpp)
        target_link_libraries(${tgt}
            acquire-video-runtime
            acquire-device-kit
            acquire-device-hal
        )
        target_compile_definitions(${tgt} PUBLIC TEST="${tgt}")
        add_test(NAME test-${tgt} COMMAND ${tgt})
        set_tests_properties(test-${tgt} PROPERTIES LABELS "anyplatform;acquire-video-runtime")
    endforeach()

    #
    # Copy driver to tests
    #
    list(POP_FRONT tests onename)
    add_custom_target(${project}-copy-${driver}-for-tests
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:${driver}>
        $<TARGET_FILE_DIR:${project}-${onename}>
        DEPENDS ${driver}
        COMMENT "Copying ${driver} to $<TARGET_FILE_DIR:${project}-${onename}>"
    )

    foreach(name ${tests})
        add_dependencies(${tgt} ${project}-copy-${driver}-for-tests)
    endforeach()
endif()
