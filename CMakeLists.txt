cmake_minimum_required(VERSION 3.20)
project(engine)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

set(OUTPUT_ROOT "${CMAKE_SOURCE_DIR}/out")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")

foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
	string(TOUPPER "${config}" config_upper)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
	set(CMAKE_PDB_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}")
endforeach()

# ─── Generate Reflection Metadata Before Building Engine ───────────────────────

# Create required directories
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/engine/generated")
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/engine/assets")

# Find the reflection generator executable
find_program(REFLECTION_GENERATOR reflection_generator
	PATHS "${CMAKE_SOURCE_DIR}/tools/reflection_generator/target/debug"
		  "${CMAKE_SOURCE_DIR}/tools/reflection_generator/target/release"
	NO_DEFAULT_PATH
)

if(NOT REFLECTION_GENERATOR)
	message(STATUS "Building reflection_generator tool...")
	find_program(CARGO cargo REQUIRED)
	execute_process(
		COMMAND ${CARGO} build
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/reflection_generator"
		RESULT_VARIABLE CARGO_BUILD_RESULT
		OUTPUT_VARIABLE CARGO_BUILD_OUTPUT
		ERROR_VARIABLE CARGO_BUILD_ERROR
	)
	if(NOT CARGO_BUILD_RESULT EQUAL 0)
		message(FATAL_ERROR "Failed to build reflection_generator:\n${CARGO_BUILD_ERROR}")
	endif()

	# Try to find it again after building
	find_program(REFLECTION_GENERATOR reflection_generator
		PATHS "${CMAKE_SOURCE_DIR}/tools/reflection_generator/target/debug"
		NO_DEFAULT_PATH
		REQUIRED
	)
endif()

message(STATUS "Using reflection_generator: ${REFLECTION_GENERATOR}")

# ALWAYS regenerate reflection files on every cmake run
message(STATUS "Cleaning old generated reflection files...")
file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/engine/generated")
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/engine/generated")

message(STATUS "Generating reflection metadata...")
execute_process(
	COMMAND ${REFLECTION_GENERATOR}
		--database "${CMAKE_SOURCE_DIR}/engine/assets/engine_reflect.json"
		--output "${CMAKE_SOURCE_DIR}/engine/generated"
		--input "${CMAKE_SOURCE_DIR}/engine/src/toast"
		--include-root "${CMAKE_SOURCE_DIR}/engine/src"
		--register-fn "registerEngineTypes"
	RESULT_VARIABLE REFGEN_RESULT
	OUTPUT_VARIABLE REFGEN_OUTPUT
	ERROR_VARIABLE REFGEN_ERROR
)

if(NOT REFGEN_RESULT EQUAL 0)
	message(FATAL_ERROR "Reflection generation failed:\n${REFGEN_ERROR}")
else()
	message(STATUS "Reflection metadata generated successfully")
	message(STATUS "${REFGEN_OUTPUT}")
endif()

# ─── End Reflection Metadata Generation ────────────────────────────────────────

add_subdirectory(engine)
add_subdirectory(tools)
add_subdirectory(editor)
add_subdirectory(tests)
