project(my_game CXX)
set(CMAKE_CXX_STANDARD 23)

file(GLOB_RECURSE SOURCE "src/*.cpp")
add_library(my_game SHARED ${SOURCE})

target_include_directories(my_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_dependencies(my_game toast_engine)
target_link_libraries(my_game PUBLIC toast_engine)
target_compile_definitions(my_game PRIVATE GAME_EXPORT)
