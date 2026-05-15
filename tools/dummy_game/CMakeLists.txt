project(dummy_game CXX)

add_library(dummy_game SHARED game.cpp)
target_link_libraries(dummy_game PUBLIC toast_engine)
target_compile_definitions(dummy_game PRIVATE GAME_EXPORT)
