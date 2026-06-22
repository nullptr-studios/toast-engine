
cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
    message(FATAL_ERROR "link_or_copy_dir: SRC_DIR and DST_DIR are required")
endif()

if(NOT EXISTS "${SRC_DIR}")
    message(WARNING "link_or_copy_dir: SRC_DIR does not exist: ${SRC_DIR}")
    return()
endif()

file(MAKE_DIRECTORY "${DST_DIR}")
file(GLOB entries LIST_DIRECTORIES false "${SRC_DIR}/*")

foreach(src IN LISTS entries)
    get_filename_component(fname "${src}" NAME)
    set(dst "${DST_DIR}/${fname}")
    if(EXISTS "${dst}" OR IS_SYMLINK "${dst}")
        file(REMOVE "${dst}")
    endif()
    file(CREATE_LINK "${src}" "${dst}" RESULT result SYMBOLIC COPY_ON_ERROR)
    if(NOT result EQUAL 0)
        message(WARNING "link_or_copy_dir: failed to deploy ${fname}: ${result}")
    endif()
endforeach()