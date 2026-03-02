add_library(glad STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gl.c
)

target_include_directories(glad PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)