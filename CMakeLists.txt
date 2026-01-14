cmake_minimum_required(VERSION 3.20)

project(toast_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TODO: add option to compile without editor features (TOAST_EDITOR, ImGui, ImGuizmo, tracy, etc.)

# If single-config generator, default to Debug when not set.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# External dependencies
add_subdirectory(external)

# Sources
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.c
)
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/inc/*.hpp
        ${CMAKE_SOURCE_DIR}/inc/*.h
        ${CMAKE_SOURCE_DIR}/inc/*.inl
)

add_library(toast.engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(toast.engine
        PUBLIC ${CMAKE_SOURCE_DIR}/inc
        PRIVATE ${CMAKE_SOURCE_DIR}/src
)

# Precompiled header
if(EXISTS "${CMAKE_SOURCE_DIR}/pch.h")
    target_precompile_headers(toast.engine PRIVATE ${CMAKE_SOURCE_DIR}/pch.h)
endif()

# Compile definitions per-config
target_compile_definitions(toast.engine PRIVATE
        NOMINMAX
        TOAST_EDITOR # TODO: add option to disable
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:RelWithDebInfo>:NDEBUG>
)

# Compiler warnings
target_compile_options(toast.engine PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
)

# Link libraries
target_link_libraries(toast.engine PUBLIC
        glfw
        glm::glm
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        lz4_static
        Tracy::TracyClient
        glad
        stb
        yaml-cpp
        sol2
        tinyobjloader
        imgui
        imguizmo
        spine-cpp
)

# Platform system libs
if(WIN32)
    target_link_libraries(toast.engine PUBLIC comdlg32 opengl32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(toast.engine PUBLIC X11 pthread dl GL)
endif()

# Tests
add_subdirectory(tests)

# Tools
add_subdirectory(tools)

