cmake_minimum_required(VERSION 3.28)
project(engine)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(OUTPUT_ROOT "${CMAKE_SOURCE_DIR}/out")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'Debug' as none was specified.")
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

if (WIN32)
	add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

if (MSVC)
	# Enable multi-processor compilation
	add_compile_options(/MP)
	if (CMAKE_GENERATOR MATCHES "Visual Studio")
		cmake_host_system_information(RESULT TOAST_CPU_COUNT QUERY NUMBER_OF_LOGICAL_CORES)
		set(CMAKE_VS_GLOBALS "UseMultiToolTask=true" "EnforceProcessCountAcrossBuilds=true" "CL_MPCount=${TOAST_CPU_COUNT}")
	endif()
	foreach(cfg IN ITEMS DEBUG RELWITHDEBINFO)
		string(REGEX REPLACE "/INCREMENTAL:NO" "/INCREMENTAL" CMAKE_SHARED_LINKER_FLAGS_${cfg} "${CMAKE_SHARED_LINKER_FLAGS_${cfg}}")
		string(REGEX REPLACE "/INCREMENTAL:NO" "/INCREMENTAL" CMAKE_EXE_LINKER_FLAGS_${cfg} "${CMAKE_EXE_LINKER_FLAGS_${cfg}}")
	endforeach()
endif()

if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
	find_program(TOAST_COMPILER_CACHE NAMES sccache ccache)
	if (TOAST_COMPILER_CACHE)
		set(CMAKE_CXX_COMPILER_LAUNCHER "${TOAST_COMPILER_CACHE}")
		set(CMAKE_C_COMPILER_LAUNCHER "${TOAST_COMPILER_CACHE}")
		# Launchers cannot cache /Zi pdb output
		set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
	endif()
endif()

foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
	string(TOUPPER "${config}" config_upper)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
	set(CMAKE_PDB_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
endforeach()

include(cmake/codegen.cmake)
run_codegen()

add_subdirectory(engine)
add_subdirectory(tools)
add_subdirectory(editor)
add_subdirectory(tests)
