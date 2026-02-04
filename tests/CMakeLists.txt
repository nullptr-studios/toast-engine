file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB TEST_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

if(TEST_SOURCES)
    add_executable(toast.test ${TEST_SOURCES} ${TEST_HEADERS})
    target_precompile_headers(toast.test REUSE_FROM toast.engine)
    
    # On Linux, force all symbols from toast.engine to be included to resolve spine-cpp dependencies
    if(UNIX AND NOT APPLE)
        target_link_libraries(toast.test PRIVATE -Wl,--whole-archive toast.engine -Wl,--no-whole-archive)
    else()
        target_link_libraries(toast.test PRIVATE toast.engine)
    endif()
    
    add_custom_command(TARGET toast.test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:toast.test>/assets
            COMMENT "Copying assets to build directory"
    )
endif()