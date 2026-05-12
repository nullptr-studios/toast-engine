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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CARGO_TARGET_DIR}/${CARGO_PROFILE_NAME}/log_server${CMAKE_EXECUTABLE_SUFFIX}"
        "${OUTPUT_ROOT}/$<CONFIG>/log_server${CMAKE_EXECUTABLE_SUFFIX}"
        WORKING_DIRECTORY ${CARGO_PROJECT_DIR}
        COMMENT "[cargo] Building log_server (${CARGO_PROFILE_NAME})..."
        VERBATIM
)
