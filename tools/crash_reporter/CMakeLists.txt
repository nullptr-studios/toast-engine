project(crash_reporter NONE)

set(CSHARP_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/crash_reporter.csproj")
set(CSHARP_ASSEMBLY_NAME "crash_reporter")

set(DOTNET_CONFIG "$<IF:$<CONFIG:Debug>,Debug,Release>")

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)

# The crash handler looks for it next to out/<Config>/toast_engine
add_custom_target(crash_reporter ALL
        COMMAND ${DOTNET_EXECUTABLE} build
        "${CSHARP_PROJECT}"
        --configuration ${DOTNET_CONFIG}
        --output "${OUTPUT_ROOT}/$<CONFIG>/crash_reporter"
        --nologo
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "[dotnet] Building ${CSHARP_ASSEMBLY_NAME} (${DOTNET_CONFIG})..."
        VERBATIM
)
