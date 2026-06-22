project(toast_player NONE)

set(CSHARP_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/player.csproj")
set(CSHARP_ASSEMBLY_NAME "player")

set(DOTNET_CONFIG "$<IF:$<CONFIG:Debug>,Debug,Release>")

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
message(STATUS "Found dotnet: ${DOTNET_EXECUTABLE}")
message(STATUS "Build configuration: ${DOTNET_CONFIG}")
message(STATUS "Output directory: ${OUTPUT_ROOT}/$<CONFIG>")

add_custom_target(player ALL
        COMMAND ${DOTNET_EXECUTABLE} build
        "${CSHARP_PROJECT}"
        --configuration ${DOTNET_CONFIG}
        --output "${OUTPUT_ROOT}/$<CONFIG>/player"
        --nologo
        COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=$<TARGET_FILE_DIR:toast_engine>
        -DDST_DIR=${OUTPUT_ROOT}/$<CONFIG>/player
        -P "${CMAKE_SOURCE_DIR}/cmake/link_or_copy_dir.cmake"
        COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=$<TARGET_FILE_DIR:dummy_game>
        -DDST_DIR=${OUTPUT_ROOT}/$<CONFIG>/player
        -P "${CMAKE_SOURCE_DIR}/cmake/link_or_copy_dir.cmake"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "[dotnet] Building ${CSHARP_ASSEMBLY_NAME} (${DOTNET_CONFIG})..."
        VERBATIM
)

add_dependencies(player toast_engine)
add_dependencies(player dummy_game)