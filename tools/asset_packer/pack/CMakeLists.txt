project(toast_pack CXX)

find_package(lz4 CONFIG REQUIRED)

add_library(toast_pack STATIC src/pack.cpp)
target_compile_features(toast_pack PUBLIC cxx_std_23)
target_include_directories(toast_pack PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(toast_pack PUBLIC LZ4::lz4)

if (MSVC)
    target_compile_options(toast_pack PRIVATE /W4 /utf-8)
else ()
    target_compile_options(toast_pack PRIVATE -Wall -Wextra)
endif ()
