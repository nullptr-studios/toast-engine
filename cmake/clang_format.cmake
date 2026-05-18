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
    "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp" "${SRC_DIR}/*.cpp "${SRC_DIR}/*.inl"
    "${FFI_DIR}/*.h" "${FFI_DIR}/*.hpp" "${FFI_DIR}/*.cpp "${FFI_DIR}/*.inl"
)

foreach(fmt_file IN LISTS FORMAT_FILES)
    execute_process(
        COMMAND "${CLANG_FORMAT}" -i "${fmt_file}"
        RESULT_VARIABLE fmt_result
    )
    if(NOT fmt_result EQUAL 0)
        message(WARNING "clang_format: clang-format failed for ${fmt_file}")
    endif()
endforeach()

