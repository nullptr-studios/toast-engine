project(asset_packer CXX)
add_subdirectory(pack)

find_package(indicators CONFIG REQUIRED)

set(PACKER_TOOLS packer unpacker pack_viewer)
foreach (tool IN LISTS PACKER_TOOLS)
    add_executable(${tool} src/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE toast_pack indicators::indicators)
    target_include_directories(${tool} PRIVATE src)
    toast_copy_runtime_dlls(${tool})

    if (MSVC)
        target_compile_definitions(${tool} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${tool} PRIVATE /W4 /utf-8 /EHsc)
    else ()
        target_compile_options(${tool} PRIVATE -Wall -Wextra)
    endif ()

    foreach (config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(${tool} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}/packer")
    endforeach ()
endforeach ()
