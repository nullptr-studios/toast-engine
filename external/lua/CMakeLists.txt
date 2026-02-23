file(GLOB LUA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/lua.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/luac.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/onelua.c"
)
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX)
    target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)
    target_link_libraries(lua_static PUBLIC m dl)
endif()
