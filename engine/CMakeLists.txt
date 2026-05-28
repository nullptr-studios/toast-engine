project(toast_engine C CXX)

find_package(asio CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

file(GLOB_RECURSE SOURCE "src/*.cpp")
add_library(toast_engine SHARED ${SOURCE})

set(TOAST_ENGINE_OUTPUT_ROOT "${OUTPUT_ROOT}/$<CONFIG>/toast_engine")
set(TOAST_ENGINE_BIN_DIR "${TOAST_ENGINE_OUTPUT_ROOT}/bin")
set(TOAST_ENGINE_LIB_DIR "${TOAST_ENGINE_OUTPUT_ROOT}/lib")

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

foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(toast_engine PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/bin"
        "LIBRARY_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/lib"
        "ARCHIVE_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/lib"
        "PDB_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/bin"
    )
endforeach()

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

add_custom_command(TARGET toast_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${TOAST_ENGINE_OUTPUT_ROOT}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ffi"
        "${TOAST_ENGINE_OUTPUT_ROOT}/ffi"
    COMMENT "Copying toast_engine headers into the package layout"
    VERBATIM
)

if (WIN32)
    add_custom_command(TARGET toast_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:toast_engine>
            "${TOAST_ENGINE_BIN_DIR}"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying toast_engine runtime DLL dependencies"
        VERBATIM
    )
endif()

install(TARGETS toast_engine
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ffi/ DESTINATION ffi)
