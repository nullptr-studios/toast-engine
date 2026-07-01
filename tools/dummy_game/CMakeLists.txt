project(dummy_game CXX)

add_library(dummy_game SHARED game.cpp)
target_link_libraries(dummy_game PUBLIC toast_engine)
target_compile_definitions(dummy_game PRIVATE GAME_EXPORT)

foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(dummy_game PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/dummy_game"
        "LIBRARY_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/dummy_game"
        "ARCHIVE_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/dummy_game"
        "PDB_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/dummy_game"
    )
endforeach()
