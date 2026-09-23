project(editor NONE)

set(CSHARP_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/editor.csproj")
set(CSHARP_ASSEMBLY_NAME "editor")

set(DOTNET_CONFIG "$<IF:$<CONFIG:Debug>,Debug,Release>")

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
message(STATUS "Found dotnet: ${DOTNET_EXECUTABLE}")
message(STATUS "Build configuration: ${DOTNET_CONFIG}")
message(STATUS "Output directory: ${OUTPUT_ROOT}/$<CONFIG>")

if(CMAKE_GENERATOR MATCHES "Visual Studio")
        add_executable(editor WIN32 "${CSHARP_PROJECT}")
        set_source_files_properties("${CSHARP_PROJECT}" PROPERTIES HEADER_FILE_ONLY TRUE)
        set_target_properties(editor PROPERTIES
                LINKER_LANGUAGE CXX
                RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_ROOT}/$<CONFIG>/editor"
                VS_CONFIGURATION_TYPE Utility
                VS_DEBUGGER_COMMAND "${OUTPUT_ROOT}/$<CONFIG>/editor/editor.exe"
                VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
                string(TOUPPER "${config}" config_upper)
                set_target_properties(editor PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}/editor"
                )
        endforeach()

        add_custom_command(TARGET editor PRE_BUILD
                COMMAND ${DOTNET_EXECUTABLE} build
                "${CSHARP_PROJECT}"
                --configuration ${DOTNET_CONFIG}
                --property:Platform=AnyCPU
                --output "${OUTPUT_ROOT}/$<CONFIG>/editor"
                --nologo
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "[dotnet] Building ${CSHARP_ASSEMBLY_NAME} (${DOTNET_CONFIG})..."
                VERBATIM
        )
else()
        add_custom_target(editor ALL
                COMMAND ${DOTNET_EXECUTABLE} build
                "${CSHARP_PROJECT}"
                --configuration ${DOTNET_CONFIG}
                --property:Platform=AnyCPU
                --output "${OUTPUT_ROOT}/$<CONFIG>/editor"
                --nologo
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "[dotnet] Building ${CSHARP_ASSEMBLY_NAME} (${DOTNET_CONFIG})..."
                VERBATIM
        )
endif()

add_dependencies(editor toast_engine)
add_dependencies(editor dummy_game)
add_dependencies(editor packer)
add_dependencies(editor unpacker)
add_dependencies(editor pack_viewer)

# Native editor crashes launch it
add_dependencies(editor crash_reporter)
