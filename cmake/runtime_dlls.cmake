function(toast_copy_runtime_dlls target)
    if (WIN32)
        set(_vcpkg_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        file(GLOB _vcpkg_dlls_debug CONFIGURE_DEPENDS "${_vcpkg_prefix}/debug/bin/*.dll")
        file(GLOB _vcpkg_dlls_release CONFIGURE_DEPENDS "${_vcpkg_prefix}/bin/*.dll")
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${target}>
                    "$<$<CONFIG:Debug>:${_vcpkg_dlls_debug}>"
                    "$<$<NOT:$<CONFIG:Debug>>:${_vcpkg_dlls_release}>"
                    $<TARGET_FILE_DIR:${target}>
                COMMAND_EXPAND_LISTS
                VERBATIM
        )
    endif ()
endfunction()
