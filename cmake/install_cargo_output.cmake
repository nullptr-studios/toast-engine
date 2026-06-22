set(_binary "${BINARY_NAME}${EXE_SUFFIX}")
set(_direct "${CARGO_TARGET_DIR}/${PROFILE_NAME}/${_binary}")

if(EXISTS "${_direct}")
    set(_src_dir "${CARGO_TARGET_DIR}/${PROFILE_NAME}")
else()
    file(GLOB _candidates "${CARGO_TARGET_DIR}/*/${PROFILE_NAME}/${_binary}")
    if(NOT _candidates)
        message(FATAL_ERROR
            "cargo output not found for '${_binary}'\n"
            "Searched:\n"
            "  ${CARGO_TARGET_DIR}/${PROFILE_NAME}/\n"
            "  ${CARGO_TARGET_DIR}/*/${PROFILE_NAME}/")
    endif()
    list(GET _candidates 0 _found)
    cmake_path(GET _found PARENT_PATH _src_dir)
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")
file(COPY "${_src_dir}/${_binary}" DESTINATION "${DEST_DIR}")
if(EXISTS "${_src_dir}/templates")
    file(COPY "${_src_dir}/templates" DESTINATION "${DEST_DIR}")
endif()
