# Copy only .generated.hpp files from SRC_DIR to DST_DIR
# Filters out .type_info.cpp, .pb.cc, .pb.h, reflect.generated.cpp

if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
    message(FATAL_ERROR "copy_generated_headers: SRC_DIR and DST_DIR required")
endif()

file(MAKE_DIRECTORY "${DST_DIR}")
file(GLOB HEADERS "${SRC_DIR}/*.generated.hpp")
foreach(hdr IN LISTS HEADERS)
    get_filename_component(name "${hdr}" NAME)
    configure_file("${hdr}" "${DST_DIR}/${name}" COPYONLY)
endforeach()
