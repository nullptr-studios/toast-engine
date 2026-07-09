cmake_minimum_required(VERSION 3.20)
project(toast_engine C CXX)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(ARCH "arm64")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(ARCH "x86_64")
else ()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif ()

if (WIN32)
    set(OS "windows")
else ()
    set(OS "linux")
endif ()

# VCPKG pacakges
find_package(asio CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Tracy CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(tinysplinecxx CONFIG REQUIRED)

# External paths
set(EXTERNAL_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/inc")
set(EXT_LIB_BASE "${CMAKE_CURRENT_SOURCE_DIR}/external/lib/${OS}/${ARCH}")

if (WIN32)
    file(GLOB EXTERNAL_LIBS_DEBUG "${EXT_LIB_BASE}/debug/*.lib")
    file(GLOB EXTERNAL_LIBS_RELEASE "${EXT_LIB_BASE}/release/*.lib")
    file(GLOB EXTERNAL_DLLS_DEBUG "${EXT_LIB_BASE}/debug/*.dll")
    file(GLOB EXTERNAL_DLLS_RELEASE "${EXT_LIB_BASE}/release/*.dll")
else ()
    file(GLOB EXTERNAL_LIBS_DEBUG "${EXT_LIB_BASE}/debug/*.so")
    file(GLOB EXTERNAL_LIBS_RELEASE "${EXT_LIB_BASE}/release/*.so")
    set(EXTERNAL_DLLS_DEBUG ${EXTERNAL_LIBS_DEBUG})
    set(EXTERNAL_DLLS_RELEASE ${EXTERNAL_LIBS_RELEASE})
endif ()

file(GLOB_RECURSE SOURCE "src/*.cpp" "generated/*.cpp" "generated/*.cc")
add_library(toast_engine SHARED ${SOURCE})

# Reflection generator
file(GLOB_RECURSE TOAST_REFLECTED_HEADERS CONFIGURE_DEPENDS "src/toast/*.hpp" "src/toast/*.h")
add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reflection.stamp"
        COMMAND "${OUTPUT_ROOT}/$<CONFIG>/reflection_generator/reflection_generator${CMAKE_EXECUTABLE_SUFFIX}"
        --database "${CMAKE_SOURCE_DIR}/engine/assets/engine_reflect.json"
        --output "${CMAKE_SOURCE_DIR}/engine/generated"
        --input "${CMAKE_SOURCE_DIR}/engine/src/toast"
        --include-root "${CMAKE_SOURCE_DIR}/engine/src"
        --register-fn "registerEngineTypes"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/reflection.stamp"
        DEPENDS ${TOAST_REFLECTED_HEADERS}
        COMMENT "Regenerating reflection metadata"
        VERBATIM
)
add_custom_target(reflection_gen DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/reflection.stamp")
add_dependencies(reflection_gen reflection_generator)
add_dependencies(toast_engine reflection_gen)

# Output layout
set(TOAST_ENGINE_OUTPUT_ROOT "${OUTPUT_ROOT}/$<CONFIG>/toast_engine")
set(TOAST_ENGINE_BIN_DIR "${TOAST_ENGINE_OUTPUT_ROOT}/bin")

target_include_directories(toast_engine
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}" "${EXTERNAL_INC_DIR}"
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/ffi"
)

# target_compile_definitions(toast_engine PRIVATE VK_NO_PROTOTYPES)
# target_compile_definitions(toast_engine PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)


target_link_libraries(toast_engine
    PRIVATE
        asio::asio
        SDL3::SDL3
        Vulkan::Vulkan
        KTX::ktx
        GPUOpen::VulkanMemoryAllocator
        unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        slang::gfx slang::slang protobuf::libprotobuf tinysplinecxx::tinysplinecxx
        $<$<CONFIG:Debug>:${EXTERNAL_LIBS_DEBUG}>
        $<$<NOT:$<CONFIG:Debug>>:${EXTERNAL_LIBS_RELEASE}>
        PUBLIC
        Tracy::TracyClient glm::glm nlohmann_json::nlohmann_json
)

foreach (config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(toast_engine PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/bin"
            "LIBRARY_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/lib"
            "ARCHIVE_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/lib"
            "PDB_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/toast_engine/bin"
    )
endforeach ()

target_compile_definitions(toast_engine PRIVATE TOAST_EXPORT)
target_precompile_headers(toast_engine PRIVATE pch.h)
target_compile_definitions(toast_engine PUBLIC
        $<$<CONFIG:Debug>:DEBUG>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:NDEBUG>
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:TRACY_ENABLE>
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:TRACY_ON_DEMAND>
)

if (MSVC)
    target_compile_options(toast_engine PRIVATE $<$<CONFIG:Release>:/O2;/Ob2;/Oi;/Ot;/GL;/Gy;/Gw;/fp:fast>)
    target_link_options(toast_engine PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif ()

target_compile_options(toast_engine PUBLIC
        $<$<CXX_COMPILER_ID:GNU>:-Wno-attributes>
        $<$<CXX_COMPILER_ID:Clang>:-Wno-attributes>
)

add_custom_target(clang_format
        COMMAND ${CMAKE_COMMAND} -DROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/clang_format.cmake
        COMMENT "Formatting sources"
        VERBATIM
)

file(GLOB_RECURSE _TOAST_API_HEADERS "src/*.h" "src/*.hpp")

add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sync_api_headers.stamp"
        COMMAND ${CMAKE_COMMAND} -DROOT_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/sync_api_headers.cmake
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/sync_api_headers.stamp"
        COMMENT "Syncing public headers"
        DEPENDS ${_TOAST_API_HEADERS} ${CMAKE_SOURCE_DIR}/cmake/sync_api_headers.cmake ${CMAKE_SOURCE_DIR}/cmake/clang_format.cmake
        VERBATIM
)

add_custom_target(sync_api_headers DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/sync_api_headers.stamp")
add_dependencies(sync_api_headers clang_format)
add_dependencies(toast_engine sync_api_headers)
add_dependencies(toast_engine log_server)

# Post build
get_target_property(_nlohmann_include_list nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
list(GET _nlohmann_include_list 0 _nlohmann_include)
get_target_property(_tracy_include_list Tracy::TracyClient INTERFACE_INCLUDE_DIRECTORIES)
list(GET _tracy_include_list 0 _tracy_include)

add_custom_command(TARGET toast_engine POST_BUILD
        # Headers
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/include" "${TOAST_ENGINE_OUTPUT_ROOT}/include"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ffi" "${TOAST_ENGINE_OUTPUT_ROOT}/ffi"
        # Dependencies
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_nlohmann_include}/nlohmann" "${TOAST_ENGINE_OUTPUT_ROOT}/include/nlohmann"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_tracy_include}/tracy" "${TOAST_ENGINE_OUTPUT_ROOT}/include/tracy"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_tracy_include}/common" "${TOAST_ENGINE_OUTPUT_ROOT}/include/common"
        # Runtimes
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<$<CONFIG:Debug>:${EXTERNAL_DLLS_DEBUG}>"
        "$<$<NOT:$<CONFIG:Debug>>:${EXTERNAL_DLLS_RELEASE}>"
        "${TOAST_ENGINE_BIN_DIR}"
        COMMENT "Copying package layout and external runtimes"
        COMMAND_EXPAND_LISTS
        VERBATIM
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_custom_command(TARGET toast_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${TOAST_ENGINE_BIN_DIR}/assets"
            COMMENT "Copying assets"
            VERBATIM
    )
endif ()

if (WIN32)
    add_custom_command(TARGET toast_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:toast_engine> "${TOAST_ENGINE_BIN_DIR}"
            COMMENT "Copying runtime DLLs"
            COMMAND_EXPAND_LISTS
            VERBATIM
    )
endif ()

install(TARGETS toast_engine RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)