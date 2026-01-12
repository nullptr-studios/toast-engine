# @file CMakeLists.txt
# @author Xein
# @date 7 Jan 2026

cmake_minimum_required(VERSION 3.24)

# Set policy for FetchContent_Populate deprecation
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(toast_engine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Set external directory for FetchContent
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/external")

include(FetchContent)

# Global compile definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(NDEBUG GLM_FORCE_INTRINSICS)
endif()

# Disable building tests for dependencies
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ============================================================================
# Dependencies via FetchContent
# ============================================================================

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

# GLM (header-only)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

# nlohmann_json (header-only)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

# spdlog (header-only mode)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
)

# LZ4
set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG v1.10.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake
)

# Tracy
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.11.1
    GIT_SHALLOW TRUE
)

# GLAD
set(GLAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/glad_gen")
file(MAKE_DIRECTORY "${GLAD_DIR}/include/glad" "${GLAD_DIR}/include/KHR" "${GLAD_DIR}/src")

# Download glad.h
file(DOWNLOAD
    "https://raw.githubusercontent.com/Dav1dde/glad/c/include/glad/glad.h"
    "${GLAD_DIR}/include/glad/glad.h"
    STATUS GLAD_H_STATUS
)

# Download khrplatform.h
file(DOWNLOAD
    "https://raw.githubusercontent.com/Dav1dde/glad/c/include/KHR/khrplatform.h"
    "${GLAD_DIR}/include/KHR/khrplatform.h"
    STATUS KHR_H_STATUS
)

# Download glad.c
file(DOWNLOAD
    "https://raw.githubusercontent.com/Dav1dde/glad/c/src/glad.c"
    "${GLAD_DIR}/src/glad.c"
    STATUS GLAD_C_STATUS
)

add_library(glad STATIC "${GLAD_DIR}/src/glad.c")
target_include_directories(glad PUBLIC "${GLAD_DIR}/include")

# STB (header-only)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)

# yaml-cpp
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG master
)

# Lua
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.4
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
    
    # Create lua library
    file(GLOB LUA_SOURCES 
        "${lua_SOURCE_DIR}/*.c"
    )
    # Remove lua.c, luac.c, and onelua.c
    list(REMOVE_ITEM LUA_SOURCES 
        "${lua_SOURCE_DIR}/lua.c" 
        "${lua_SOURCE_DIR}/luac.c"
        "${lua_SOURCE_DIR}/onelua.c"
    )
    
    add_library(lua_static STATIC ${LUA_SOURCES})
    target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}")
    
    if(UNIX)
        target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)
        target_link_libraries(lua_static PUBLIC m dl)
    endif()
endif()

# sol2 (header-only)
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.3.1
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(sol2)
if(NOT sol2_POPULATED)
    FetchContent_Populate(sol2)
    add_library(sol2 INTERFACE)
    target_include_directories(sol2 INTERFACE "${sol2_SOURCE_DIR}/include")
    target_compile_definitions(sol2 INTERFACE SOL_USE_CXX17_IF_AVAILABLE=1 SOL_USING_CXX_OPTIONAL=1)
    target_link_libraries(sol2 INTERFACE lua_static)
endif()

# tinyobjloader (header-only)
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
    GIT_SHALLOW TRUE
)

# ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.5-docking
    GIT_SHALLOW TRUE
)

# ImGuizmo
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG master
)

# Spine Runtime
FetchContent_Declare(
    spine
    GIT_REPOSITORY https://github.com/EsotericSoftware/spine-runtimes.git
    GIT_TAG 4.2
    GIT_SHALLOW TRUE
)

# Make all content available
message("Fetching external dependencies... please wait...")
FetchContent_MakeAvailable(glfw glm nlohmann_json spdlog lz4 tracy yaml-cpp tinyobjloader)

# Handle STB
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
    add_library(stb INTERFACE)
    # Create stb directory structure for includes
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/stb_include/stb")
    file(GLOB STB_HEADERS "${stb_SOURCE_DIR}/*.h")
    file(COPY ${STB_HEADERS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/stb_include/stb")
    target_include_directories(stb INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/stb_include")
endif()

# Handle ImGui
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    )
    target_include_directories(imgui PUBLIC 
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imgui_SOURCE_DIR}/misc/cpp
    )
    target_link_libraries(imgui PUBLIC glfw)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)
endif()

# Handle ImGuizmo
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
    add_library(imguizmo STATIC
        ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
        ${imguizmo_SOURCE_DIR}/ImCurveEdit.cpp
        ${imguizmo_SOURCE_DIR}/ImGradient.cpp
        ${imguizmo_SOURCE_DIR}/ImSequencer.cpp
    )
    target_include_directories(imguizmo PUBLIC ${imguizmo_SOURCE_DIR})
    target_link_libraries(imguizmo PUBLIC imgui)
endif()

# Handle Spine
FetchContent_GetProperties(spine)
if(NOT spine_POPULATED)
    FetchContent_Populate(spine)
    file(GLOB SPINE_SOURCES 
        "${spine_SOURCE_DIR}/spine-cpp/spine-cpp/src/spine/*.cpp"
    )
    add_library(spine-cpp STATIC ${SPINE_SOURCES})
    target_include_directories(spine-cpp PUBLIC 
        "${spine_SOURCE_DIR}/spine-cpp/spine-cpp/include"
    )
endif()

# ============================================================================
# Main Library Target
# ============================================================================

file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp")
file(GLOB_RECURSE ENGINE_HEADERS "inc/*.hpp" "inc/*.h" "inc/*.inl")

add_library(toast.engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(toast.engine 
    PUBLIC inc
    PRIVATE src
)

target_precompile_headers(toast.engine PRIVATE pch.h)

target_compile_definitions(toast.engine PRIVATE 
    TOAST_EDITOR
    NOMINMAX  # Prevent Windows.h from defining min/max macros
)

target_link_libraries(toast.engine PUBLIC
    glfw
    glm::glm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    lz4_static
    TracyClient
    glad
    stb
    yaml-cpp
    sol2
    tinyobjloader
    imgui
    imguizmo
    spine-cpp
)

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(toast.engine PUBLIC comdlg32 opengl32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(toast.engine PUBLIC X11 pthread dl GL)
endif()

# ============================================================================
# Test Application Target
# ============================================================================

file(GLOB TEST_SOURCES "tests/*.cpp")
file(GLOB TEST_HEADERS "tests/*.hpp")

add_executable(toast.test ${TEST_SOURCES} ${TEST_HEADERS})

target_precompile_headers(toast.test REUSE_FROM toast.engine)

target_link_libraries(toast.test PRIVATE toast.engine)

# Copy assets to build directory after build
add_custom_command(TARGET toast.test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:toast.test>/assets
    COMMENT "Copying assets to build directory"
)


message("Compiling Toast Packer and Unpacker tools...")
add_executable(toast.packer tools/ToastPacker/Packer.cpp)
target_link_libraries(toast.packer lz4)
add_executable(toast.unpacker tools/ToastPacker/Unpacker.cpp)
target_link_libraries(toast.unpacker lz4)
