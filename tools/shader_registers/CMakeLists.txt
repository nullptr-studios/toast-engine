project(shader_registers CXX C)

# SPIRV-Reflect ships only as source in the Vulkan SDK
find_path(SPIRV_REFLECT_DIR spirv_reflect.c
        HINTS "$ENV{VULKAN_SDK}/Source/SPIRV-Reflect" "$ENV{VULKAN_SDK}/../source/SPIRV-Reflect"
        NO_DEFAULT_PATH)
if (NOT SPIRV_REFLECT_DIR)
    message(STATUS "shader_registers skipped, no SPIRV-Reflect source under VULKAN_SDK")
    return()
endif ()

find_package(Vulkan REQUIRED)

add_executable(shader_registers EXCLUDE_FROM_ALL shader_registers.cpp "${SPIRV_REFLECT_DIR}/spirv_reflect.c")
target_include_directories(shader_registers PRIVATE "${SPIRV_REFLECT_DIR}")
target_link_libraries(shader_registers PRIVATE Vulkan::Vulkan)
target_compile_features(shader_registers PRIVATE cxx_std_20)

foreach (config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(shader_registers PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/shader_registers"
            "PDB_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/shader_registers"
    )
endforeach ()
