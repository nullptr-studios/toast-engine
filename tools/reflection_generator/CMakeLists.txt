project(reflection_generator NONE)

set(DOTNET_CONFIG "$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>,Release,Debug>")

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
message(STATUS "Found dotnet: ${DOTNET_EXECUTABLE}")

add_custom_target(reflection_generator ALL
        COMMAND ${DOTNET_EXECUTABLE} build
            "${CMAKE_CURRENT_SOURCE_DIR}/reflection_generator.csproj"
            --configuration ${DOTNET_CONFIG}
            --output "${OUTPUT_ROOT}/$<CONFIG>/reflection_generator"
            --nologo
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "[dotnet] Building reflection_generator (${DOTNET_CONFIG})..."
        VERBATIM
)
