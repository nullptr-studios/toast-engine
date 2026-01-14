file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
file(GLOB TEST_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.hpp)

if(TEST_SOURCES)
    add_executable(toast.test ${TEST_SOURCES} ${TEST_HEADERS})
    target_precompile_headers(toast.test REUSE_FROM toast.engine)
    target_link_libraries(toast.test PRIVATE toast.engine)
    
    add_custom_command(TARGET toast.test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:toast.test>/assets
            COMMENT "Copying assets to build directory"
    )
endif()