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

add_subdirectory(engine)
add_subdirectory(tools)
add_subdirectory(editor)
add_subdirectory(tests)
