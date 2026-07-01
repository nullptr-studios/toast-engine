project(log_server NONE)

set(CARGO_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CARGO_TARGET_DIR "${OUTPUT_ROOT}/$<CONFIG>/cargo")
set(CARGO_PROFILE_NAME "$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>,release,debug>")
set(CARGO_PROFILE_FLAG "$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>,release,dev>")

find_program(CARGO_EXECUTABLE cargo REQUIRED)
message(STATUS "Found cargo: ${CARGO_EXECUTABLE}")
message(STATUS "Build configuration: ${CARGO_PROFILE_NAME}")
message(STATUS "Cargo target directory: ${CARGO_TARGET_DIR}")

add_custom_target(log_server ALL
        COMMAND ${CARGO_EXECUTABLE} build --profile "${CARGO_PROFILE_FLAG}" --target-dir "${CARGO_TARGET_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -DCARGO_TARGET_DIR=${CARGO_TARGET_DIR}
            -DPROFILE_NAME=${CARGO_PROFILE_NAME}
            -DBINARY_NAME=log_server
            -DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -DDEST_DIR=${OUTPUT_ROOT}/$<CONFIG>/toast_engine/bin
            -P ${CMAKE_SOURCE_DIR}/cmake/install_cargo_output.cmake
        WORKING_DIRECTORY ${CARGO_PROJECT_DIR}
        COMMENT "[cargo] Building log_server (${CARGO_PROFILE_NAME})..."
        VERBATIM
)
