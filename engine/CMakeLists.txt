project(toast_engine C CXX)

# Package declarations
find_package(asio CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)

file(GLOB_RECURSE SOURCE "src/*.cpp" "generated/*.cpp" "generated/*.cc")
add_library(toast_engine SHARED ${SOURCE})

# Re-run the reflection generator at build time whenever an engine header changes
file(GLOB_RECURSE TOAST_REFLECTED_HEADERS CONFIGURE_DEPENDS "src/toast/*.hpp" "src/toast/*.h")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reflection.stamp"
    COMMAND ${REFLECTION_GENERATOR}
        --database "${CMAKE_SOURCE_DIR}/engine/assets/engine_reflect.json"
        --output   "${CMAKE_SOURCE_DIR}/engine/generated"
        --input    "${CMAKE_SOURCE_DIR}/engine/src/toast"
        --include-root "${CMAKE_SOURCE_DIR}/engine/src"
        --register-fn "registerEngineTypes"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/reflection.stamp"
    DEPENDS ${TOAST_REFLECTED_HEADERS}
    COMMENT "Regenerating reflection metadata"
    VERBATIM
)
add_custom_target(reflection_gen DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/reflection.stamp")
add_dependencies(toast_engine reflection_gen)

set(TOAST_ENGINE_OUTPUT_ROOT "${OUTPUT_ROOT}/$<CONFIG>/toast_engine")
set(TOAST_ENGINE_BIN_DIR "${TOAST_ENGINE_OUTPUT_ROOT}/bin")
set(TOAST_ENGINE_LIB_DIR "${TOAST_ENGINE_OUTPUT_ROOT}/lib")

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
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/ffi
)

# target_compile_definitions(toast_engine PRIVATE VK_NO_PROTOTYPES)
# target_compile_definitions(toast_engine PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)


target_link_libraries(toast_engine
    PRIVATE
        asio::asio
        SDL3::SDL3
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        slang::gfx slang::slang
        protobuf::libprotobuf
    PUBLIC
        Tracy::TracyClient
        glm::glm
        
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

if (MSVC)
    # MSVC: config-aware definitions and aggressive Release optimizations
    target_compile_definitions(toast_engine PUBLIC
        $<$<CONFIG:Debug>:DEBUG;TRACY_ENABLE>
        $<$<CONFIG:RelWithDebInfo>:TRACY_ENABLE>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:NDEBUG>
    )
    target_compile_options(toast_engine PRIVATE
        $<$<CONFIG:Release>:/O2;/Ob2;/Oi;/Ot;/GL;/Gy;/Gw;/fp:fast>
    )
    target_link_options(toast_engine PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
else()
    # GCC/Clang: CMAKE_BUILD_TYPE-based logic
    if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_definitions(toast_engine PUBLIC TRACY_ENABLE DEBUG)
    else()
        target_compile_definitions(toast_engine PUBLIC NDEBUG)
    endif()
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET toast_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "${TOAST_ENGINE_BIN_DIR}/assets"
        COMMENT "Copying toast_engine assets"
        VERBATIM
    )
endif()

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
