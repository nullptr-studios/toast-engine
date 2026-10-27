add_subdirectory(player)
add_subdirectory(dummy_game)
add_subdirectory(log_server)
add_subdirectory(reflection_generator)
add_subdirectory(asset_packer)
add_subdirectory(crash_reporter)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kenzo/CMakeLists.txt")
    message(FATAL_ERROR "kenzo submodule is empty, run git submodule update --init")
endif ()

set(KENZO_ARGS -G "${CMAKE_GENERATOR}" -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DCMAKE_BUILD_TYPE=$<CONFIG>)
if (CMAKE_GENERATOR_PLATFORM)
    list(APPEND KENZO_ARGS -A ${CMAKE_GENERATOR_PLATFORM})
endif ()
if (WIN32)
    list(APPEND KENZO_ARGS -DVCPKG_TARGET_TRIPLET=x64-windows-static)
endif ()
foreach (config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    list(APPEND KENZO_ARGS -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper}=${OUTPUT_ROOT}/${config}/kenzo)
endforeach ()

add_custom_target(kenzo ALL
        COMMAND ${CMAKE_COMMAND} -S "${CMAKE_CURRENT_SOURCE_DIR}/kenzo" -B "${OUTPUT_ROOT}/$<CONFIG>/kenzo-build" ${KENZO_ARGS}
        COMMAND ${CMAKE_COMMAND} --build "${OUTPUT_ROOT}/$<CONFIG>/kenzo-build" --config $<CONFIG> --target kenzo
        COMMENT "Building kenzo"
        VERBATIM
)

add_dependencies(reflection_generator packer)

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
