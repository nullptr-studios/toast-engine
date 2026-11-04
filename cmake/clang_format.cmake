if(NOT DEFINED ROOT_DIR)
    message(FATAL_ERROR "clang_format: ROOT_DIR not set")
endif()

set(SRC_DIR "${ROOT_DIR}/src")
set(FFI_DIR "${ROOT_DIR}/ffi")

if(NOT IS_DIRECTORY "${SRC_DIR}")
    message(STATUS "clang_format: src dir not found: ${SRC_DIR}")
    return()
endif()

find_program(CLANG_FORMAT NAMES clang-format)
if(NOT CLANG_FORMAT)
    message(WARNING "clang_format: clang-format not found; skipping formatting")
    return()
endif()

file(GLOB_RECURSE FORMAT_FILES
    "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp" "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.inl"
    "${FFI_DIR}/*.h" "${FFI_DIR}/*.hpp" "${FFI_DIR}/*.cpp" "${FFI_DIR}/*.inl"
)

cmake_host_system_information(RESULT _jobs QUERY NUMBER_OF_LOGICAL_CORES)
list(LENGTH FORMAT_FILES _count)
if(_count EQUAL 0)
    return()
endif()
if(_jobs GREATER _count)
    set(_jobs ${_count})
endif()

# One clang-format per chunk, the commands of a single execute_process run concurrently
set(_commands "")
foreach(_i RANGE 1 ${_jobs})
    set(_chunk "")
    set(_idx ${_i})
    math(EXPR _idx "${_idx} - 1")
    while(_idx LESS _count)
        list(GET FORMAT_FILES ${_idx} _f)
        list(APPEND _chunk "${_f}")
        math(EXPR _idx "${_idx} + ${_jobs}")
    endwhile()
    list(APPEND _commands COMMAND "${CLANG_FORMAT}" -i ${_chunk})
endforeach()

execute_process(
    ${_commands}
    RESULTS_VARIABLE fmt_results
    OUTPUT_QUIET
)
foreach(fmt_result IN LISTS fmt_results)
    if(NOT fmt_result EQUAL 0)
        message(WARNING "clang_format: clang-format failed (${fmt_result})")
    endif()
endforeach()

