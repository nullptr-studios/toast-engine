add_subdirectory(player)
add_subdirectory(dummy_game)
add_subdirectory(kenzo)
add_subdirectory(log_server)
add_subdirectory(reflection_generator)
add_subdirectory(asset_packer)

if (BUILD_TESTING)
    find_program(CARGO cargo)
    if (CARGO)
        add_test(
            NAME refgen_unit_tests
            COMMAND ${CARGO} test --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/reflection_generator/Cargo.toml"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/reflection_generator"
        )
        set_tests_properties(refgen_unit_tests PROPERTIES
            LABELS "reflection_generator;rust"
            TIMEOUT 300
        )
    else()
        message(WARNING "cargo not found; skipping reflection_generator unit tests")
    endif()
endif()
