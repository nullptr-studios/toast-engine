    file(GLOB SPINE_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/spine-cpp/src/spine/*.cpp"
    )
    add_library(spine-cpp STATIC ${SPINE_SOURCES})
    target_include_directories(spine-cpp PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/spine-cpp/include"
    )