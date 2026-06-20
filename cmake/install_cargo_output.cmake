# Installs a cargo binary and optional templates into DEST_DIR.
#
# Handles both output layouts:
#   <CARGO_TARGET_DIR>/<PROFILE_NAME>/          (no explicit --target)
#   <CARGO_TARGET_DIR>/<triple>/<PROFILE_NAME>/ (--target set via config.toml)
#
# Required variables (pass via cmake -D):
#   CARGO_TARGET_DIR  - value of --target-dir given to cargo
#   PROFILE_NAME      - "release" or "debug"
#   BINARY_NAME       - executable name without suffix
#   EXE_SUFFIX        - platform suffix ("" or ".exe")
#   DEST_DIR          - installation directory

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
