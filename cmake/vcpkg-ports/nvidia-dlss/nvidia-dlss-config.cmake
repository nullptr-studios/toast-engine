get_filename_component(_nvidia_dlss_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET nvidia::dlss)
    # The NGX loader is a static library, nvngx_dlss.dll is loaded by it at run time and never linked
    add_library(nvidia::dlss STATIC IMPORTED)
    set_target_properties(nvidia::dlss PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_nvidia_dlss_prefix}/include/ngx"
        IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
        IMPORTED_LOCATION_RELEASE "${_nvidia_dlss_prefix}/lib/nvsdk_ngx_d.lib"
        IMPORTED_LOCATION_DEBUG "${_nvidia_dlss_prefix}/debug/lib/nvsdk_ngx_d_dbg.lib"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RELEASE"
        MAP_IMPORTED_CONFIG_MINSIZEREL "RELEASE"
    )
endif()

set(NVIDIA_DLSS_RUNTIME_DLL "${_nvidia_dlss_prefix}/bin/nvngx_dlss.dll")
unset(_nvidia_dlss_prefix)
