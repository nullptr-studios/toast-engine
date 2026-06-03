project(editor NONE)

set(CSHARP_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/editor.csproj")
set(CSHARP_ASSEMBLY_NAME "editor")

set(DOTNET_CONFIG "$<IF:$<CONFIG:Debug>,Debug,Release>")

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
message(STATUS "Found dotnet: ${DOTNET_EXECUTABLE}")
message(STATUS "Build configuration: ${DOTNET_CONFIG}")
message(STATUS "Output directory: ${OUTPUT_ROOT}/$<CONFIG>")

add_custom_target(editor ALL
        COMMAND ${DOTNET_EXECUTABLE} build
        "${CSHARP_PROJECT}"
        --configuration ${DOTNET_CONFIG}
        --output "${OUTPUT_ROOT}/$<CONFIG>/editor"
        --nologo
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "[dotnet] Building ${CSHARP_ASSEMBLY_NAME} (${DOTNET_CONFIG})..."
        VERBATIM
)

add_dependencies(editor toast_engine)
add_dependencies(editor dummy_game)
