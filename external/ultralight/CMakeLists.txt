
add_library(ultralight INTERFACE)
target_include_directories(ultralight INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/ultralight/include
)
if (WIN32)
    target_link_libraries(ultralight INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/win/Ultralight.lib
            ${CMAKE_CURRENT_SOURCE_DIR}/win/UltralightCore.lib
            ${CMAKE_CURRENT_SOURCE_DIR}/win/WebCore.lib
            ${CMAKE_CURRENT_SOURCE_DIR}/win/AppCore.lib
    )
elseif (UNIX AND NOT APPLE)
    target_link_libraries(ultralight INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/linux/libUltralight.so
            ${CMAKE_CURRENT_SOURCE_DIR}/linux/libUltralightCore.so
            ${CMAKE_CURRENT_SOURCE_DIR}/linux/libWebCore.so
            ${CMAKE_CURRENT_SOURCE_DIR}/linux/libAppCore.so
    )
endif()
