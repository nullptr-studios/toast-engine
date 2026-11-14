project(reflection_generator NONE)

set(DOTNET_CONFIG "$<IF:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>,Release,Debug>")

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
message(STATUS "Found dotnet: ${DOTNET_EXECUTABLE}")

add_custom_target(reflection_generator ALL
        COMMAND ${DOTNET_EXECUTABLE} msbuild -restore
            "${CMAKE_CURRENT_SOURCE_DIR}/reflection_generator.csproj"
            -property:Configuration=${DOTNET_CONFIG}
            "-property:OutputPath=${OUTPUT_ROOT}/$<CONFIG>/reflection_generator/"
            -property:_CommandLineDefinedOutputPath=true
            -nologo
            -verbosity:quiet
            -consoleloggerparameters:NoSummary
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "[dotnet] Building reflection_generator (${DOTNET_CONFIG})..."
        VERBATIM
)
