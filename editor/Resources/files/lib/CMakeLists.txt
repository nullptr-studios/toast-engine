cmake_minimum_required(VERSION 3.21)
project(my_game CXX)
set(CMAKE_CXX_STANDARD 23)

if (NOT TOAST_PATH)
  message(FATAL_ERROR "TOAST_PATH not defined! Please pass -DTOAST_PATH=\"path/to/toast/engine\"")
endif ()


get_filename_component(BUILD_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/../build" ABSOLUTE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BUILD_OUTPUT})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BUILD_OUTPUT})
set(CMAKE_PDB_OUTPUT_DIRECTORY ${BUILD_OUTPUT})

foreach (OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BUILD_OUTPUT})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BUILD_OUTPUT})
  set(CMAKE_PDB_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${BUILD_OUTPUT})
endforeach ()

add_library(toast_engine SHARED IMPORTED)
set_target_properties(toast_engine PROPERTIES
        IMPORTED_IMPLIB "${TOAST_PATH}/lib/toast_engine.lib"
        IMPORTED_LOCATION "${TOAST_PATH}/bin/toast_engine.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${TOAST_PATH}/include"
)

file(GLOB_RECURSE SOURCE "src/*.cpp" "generated/*.cpp")
add_library(my_game SHARED ${SOURCE})
target_include_directories(my_game PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

target_compile_options(my_game PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wno-unknown-attributes>
        $<$<CXX_COMPILER_ID:GNU>:-Wno-attributes>
        $<$<CXX_COMPILER_ID:MSVC>:/wd5030>
)

target_link_libraries(my_game PUBLIC toast_engine)
target_compile_definitions(my_game PRIVATE GAME_EXPORT)

