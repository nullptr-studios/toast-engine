project(toast_engine C CXX)

# Package declarations
find_package(asio CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

file(GLOB_RECURSE SOURCE "src/*.cpp")
add_library(toast_engine SHARED ${SOURCE})

if (WIN32)
    # Ensure runtime DLLs are copied to the output directory
    add_custom_command(TARGET toast_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:toast_engine>
            $<TARGET_FILE_DIR:toast_engine>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs to output directory"
    )
endif()

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
    PUBLIC
        Tracy::TracyClient
        glm::glm
)

target_compile_definitions(toast_engine PRIVATE TOAST_EXPORT)
target_precompile_headers(toast_engine PRIVATE pch.h)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    # We will activate tracy on Debug and RelWithInfo
    target_compile_definitions(toast_engine PUBLIC TRACY_ENABLE DEBUG)
endif()

add_custom_target(clang_format
    COMMAND ${CMAKE_COMMAND} -DROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/clang_format.cmake
    COMMENT "Formatting sources"
    VERBATIM
)

file(GLOB_RECURSE _TOAST_API_HEADERS "src/*.h" "src/*.hpp")

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sync_api_headers.stamp"
    COMMAND ${CMAKE_COMMAND} -DROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/sync_api_headers.cmake
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/sync_api_headers.stamp"
    COMMENT "Syncing public headers"
    DEPENDS ${_TOAST_API_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/sync_api_headers.cmake ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/clang_format.cmake
    VERBATIM
)

add_custom_target(sync_api_headers DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/sync_api_headers.stamp")
add_dependencies(sync_api_headers clang_format)
add_dependencies(toast_engine sync_api_headers)
add_dependencies(toast_engine log_server)

