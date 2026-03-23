
include(FetchContent)

# Set policy for FetchContent_Populate deprecation
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# verbose
set(FETCHCONTENT_QUIET OFF)

#ultralight
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ultralight")


set(GLFW_BUILD_DOCS OFF CACHE BOOL "GLFW_BUILD_DOCS" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "GLFW_BUILD_TESTS" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "GLFW_BUILD_EXAMPLES" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "GLFW_INSTALL" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/glfw")


add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/glm")


set(JSON_BuildTests OFF CACHE BOOL "JSON_BuildTests" OFF)
set(JSON_Install OFF CACHE BOOL "JSON_Install" OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json")


# set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "SPDLOG_FMT_EXTERNAL" OFF)
set(SPDLOG_INSTALL OFF CACHE BOOL "SPDLOG_INSTALL" OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/spdlog")


set(LZ4_BUILD_CLI OFF CACHE BOOL "LZ4_BUILD_CLI" OFF)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "LZ4_BUILD_LEGACY_LZ4C" OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/lz4/amongas/cmake")


set(TRACY_ENABLE ON CACHE BOOL "Enable tracy client" ON)
set(TRACY_ON_DEMAND ON CACHE BOOL "Tracy on demand" ON)
set(TRACY_FIBERS ON CACHE BOOL "Tracy Fibers" ON)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/tracy")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/stb")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/yaml-cpp")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/lua")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/sol2")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/imgui")

# ImGuizmo
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/imguizmo")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/spine")

#fmod
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/fmod")

# glad
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/glad")
