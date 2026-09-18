if(NOT BINARY_NAMES)
    message(FATAL_ERROR "install_cargo_output: BINARY_NAMES is empty")
endif()

# cargo uses <target>/<triple>/<profile> when a target triple is configured
list(GET BINARY_NAMES 0 _first)
set(_probe "${_first}${EXE_SUFFIX}")

if(EXISTS "${CARGO_TARGET_DIR}/${PROFILE_NAME}/${_probe}")
    set(_src_dir "${CARGO_TARGET_DIR}/${PROFILE_NAME}")
else()
    file(GLOB _candidates "${CARGO_TARGET_DIR}/*/${PROFILE_NAME}/${_probe}")
    if(NOT _candidates)
        message(FATAL_ERROR
            "cargo output not found for '${_probe}'\n"
            "Searched:\n"
            "  ${CARGO_TARGET_DIR}/${PROFILE_NAME}/\n"
            "  ${CARGO_TARGET_DIR}/*/${PROFILE_NAME}/")
    endif()
    list(GET _candidates 0 _found)
    cmake_path(GET _found PARENT_PATH _src_dir)
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")

foreach(_name IN LISTS BINARY_NAMES)
    set(_binary "${_name}${EXE_SUFFIX}")
    if(NOT EXISTS "${_src_dir}/${_binary}")
        message(FATAL_ERROR "cargo output not found: ${_src_dir}/${_binary}")
    endif()
    # file(COPY) compares timestamps so unchanged binaries are not rewritten
    file(COPY "${_src_dir}/${_binary}" DESTINATION "${DEST_DIR}")
endforeach()

if(EXISTS "${_src_dir}/templates")
    file(COPY "${_src_dir}/templates" DESTINATION "${DEST_DIR}")
endif()
