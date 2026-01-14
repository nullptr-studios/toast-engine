
include(FetchContent)

# Set policy for FetchContent_Populate deprecation
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# verbose
set(FETCHCONTENT_QUIET OFF)


set(GLFW_BUILD_DOCS OFF CACHE BOOL "GLFW_BUILD_DOCS" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "GLFW_BUILD_TESTS" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "GLFW_BUILD_EXAMPLES" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "GLFW_INSTALL" FORCE)

FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)


FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)


set(JSON_BuildTests OFF CACHE BOOL "JSON_BuildTests" OFF)
set(JSON_Install OFF CACHE BOOL "JSON_Install" OFF)
FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)


# set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "SPDLOG_FMT_EXTERNAL" OFF)
set(SPDLOG_INSTALL OFF CACHE BOOL "SPDLOG_INSTALL" OFF)
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)


set(LZ4_BUILD_CLI OFF CACHE BOOL "LZ4_BUILD_CLI" OFF)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "LZ4_BUILD_LEGACY_LZ4C" OFF)
FetchContent_Declare(
        lz4
        GIT_REPOSITORY https://github.com/lz4/lz4.git
        GIT_TAG v1.10.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(lz4)


set(TRACY_ENABLE ON CACHE BOOL "Enable tracy client" ON)
set(TRACY_ON_DEMAND OFF CACHE BOOL "Tracy on demand" OFF)
FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.11.1
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tracy)


FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "YAML_CPP_BUILD_TESTS" OFF)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "YAML_CPP_BUILD_TOOLS" OFF)
set(YAML_CPP_INSTALL OFF CACHE BOOL "YAML_CPP_INSTALL" OFF)
FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
        lua
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG v5.4.4
        GIT_SHALLOW TRUE
)
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
    file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
    list(REMOVE_ITEM LUA_SOURCES
            "${lua_SOURCE_DIR}/lua.c"
            "${lua_SOURCE_DIR}/luac.c"
            "${lua_SOURCE_DIR}/onelua.c"
    )
    add_library(lua_static STATIC ${LUA_SOURCES})
    target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
    if(UNIX)
        target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)
        target_link_libraries(lua_static PUBLIC m dl)
    endif()
endif()

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
    target_include_directories(sol2 INTERFACE ${sol2_SOURCE_DIR}/include)
    target_compile_definitions(sol2 INTERFACE SOL_USE_CXX17_IF_AVAILABLE=1 SOL_USING_CXX_OPTIONAL=1)
    target_link_libraries(sol2 INTERFACE lua_static)
endif()


FetchContent_Declare(
        tinyobjloader
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG v2.0.0rc13
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tinyobjloader)


FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.5-docking
        GIT_SHALLOW TRUE
)

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

# ImGuizmo
FetchContent_Declare(
        imguizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG master
)

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


# Spine Runtime
FetchContent_Declare(
        spine
        GIT_REPOSITORY https://github.com/EsotericSoftware/spine-runtimes.git
        GIT_TAG 4.2
        GIT_SHALLOW TRUE
)

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


# glad
add_library(glad STATIC
        ${CMAKE_SOURCE_DIR}/external/glad/src/gl.c
)

target_include_directories(glad PUBLIC
        ${CMAKE_SOURCE_DIR}/external/glad/include
)

