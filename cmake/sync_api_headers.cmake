if(NOT DEFINED ROOT_DIR)
    message(FATAL_ERROR "sync_api_headers: ROOT_DIR not set")
endif()

set(SRC_DIR "${ROOT_DIR}/src")
set(INCLUDE_DIR "${ROOT_DIR}/include")

set(NOTICE [=[// ============================================================
// AUTO-GENERATED FILE - DO NOT MODIFY DIRECTLY
// Changes will not persist
// ============================================================]=])

if(NOT IS_DIRECTORY "${SRC_DIR}")
    message(STATUS "sync_api_headers: src dir not found: ${SRC_DIR}")
    return()
endif()

if(EXISTS "${INCLUDE_DIR}")
    file(REMOVE_RECURSE "${INCLUDE_DIR}")
endif()
file(MAKE_DIRECTORY "${INCLUDE_DIR}")

file(GLOB_RECURSE HEADER_FILES "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp")
foreach(header IN LISTS HEADER_FILES)
    file(READ "${header}" CONTENT)
    if(CONTENT MATCHES "TOAST_API")
        file(RELATIVE_PATH REL_PATH "${SRC_DIR}" "${header}")
        set(dst_path "${INCLUDE_DIR}/${REL_PATH}")
        get_filename_component(dst_dir "${dst_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${dst_dir}")
        file(WRITE "${dst_path}" "${NOTICE}\n${CONTENT}")

        string(REGEX MATCHALL "#include[ \t]+\"([^\"]+\\.inl)\"" INL_MATCHES "${CONTENT}")
        if(INL_MATCHES)
            get_filename_component(header_dir "${header}" DIRECTORY)
            foreach(inl_match IN LISTS INL_MATCHES)
                string(REGEX REPLACE "^#include[ \t]+\"([^\"]+\\.inl)\".*" "\\1" inl_rel "${inl_match}")
                set(inl_src "${header_dir}/${inl_rel}")
                if(EXISTS "${inl_src}")
                    file(RELATIVE_PATH inl_rel_path "${SRC_DIR}" "${inl_src}")
                    set(inl_dst "${INCLUDE_DIR}/${inl_rel_path}")
                    get_filename_component(inl_dst_dir "${inl_dst}" DIRECTORY)
                    file(MAKE_DIRECTORY "${inl_dst_dir}")
                    file(READ "${inl_src}" inl_content)
                    file(WRITE "${inl_dst}" "${NOTICE}\n${inl_content}")
                endif()
            endforeach()
        endif()
    endif()
endforeach()

