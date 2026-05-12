project(toast_engine C CXX)

find_package(asio CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

file(GLOB_RECURSE SOURCE "src/*.cpp")
add_library(toast_engine SHARED ${SOURCE})

target_include_directories(toast_engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external
        ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ffi
)

target_link_libraries(toast_engine
    PRIVATE
        asio::asio
        SDL3::SDL3
)

target_compile_definitions(toast_engine PRIVATE TOAST_EXPORT)
target_precompile_headers(toast_engine PRIVATE pch.h)

add_custom_target(clang_format
    COMMAND ${CMAKE_COMMAND} -DROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/clang_format.cmake
    COMMENT "Formatting sources"
    VERBATIM
)

add_custom_target(sync_api_headers
    COMMAND ${CMAKE_COMMAND} -DROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/sync_api_headers.cmake
    COMMENT "Syncing public headers"
    VERBATIM
)
add_dependencies(sync_api_headers clang_format)
add_dependencies(toast_engine sync_api_headers)
add_dependencies(toast_engine log_server)

