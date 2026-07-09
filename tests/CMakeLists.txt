include(CTest)

if (BUILD_TESTING)
	file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
	list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_runner.cpp")

	add_executable(toast_tests ${TEST_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/test_runner.cpp" "${CMAKE_SOURCE_DIR}/engine/src/toast/assets/prefab.cpp")
	target_include_directories(toast_tests BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
	target_include_directories(toast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	# HACK: DARIO SHOULD FUCKING HAVE SOME INCLUDES ON THE SOURCE FILES RATHER THAN THE HEADERS
	target_include_directories(toast_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine)
	target_include_directories(toast_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/external/inc)
	target_link_libraries(toast_tests PRIVATE toast_engine)
	target_compile_definitions(toast_tests PRIVATE UNIT_TESTING TRACY_NO_INVARIANT_CHECK=1)

	foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
		string(TOUPPER "${config}" config_upper)
		set_target_properties(toast_tests PROPERTIES
			"RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${OUTPUT_ROOT}/${config}/tests"
			"PDB_OUTPUT_DIRECTORY_${config_upper}"     "${OUTPUT_ROOT}/${config}/tests"
		)
	endforeach()

	if(WIN32)
		add_custom_command(TARGET toast_tests POST_BUILD
			COMMAND ${CMAKE_COMMAND}
				-DSRC_DIR=$<TARGET_FILE_DIR:toast_engine>
				-DDST_DIR=$<TARGET_FILE_DIR:toast_tests>
				-P "${CMAKE_SOURCE_DIR}/cmake/link_or_copy_dir.cmake"
			COMMENT "Deploying engine runtime to tests/"
			VERBATIM
		)
	else()
		set_target_properties(toast_tests PROPERTIES
			BUILD_RPATH "$<TARGET_FILE_DIR:toast_engine>"
		)
	endif()

	if (NOT WIN32)
		target_link_libraries(toast_tests PRIVATE stdc++exp)
	elseif (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_link_libraries(toast_tests PRIVATE stdc++exp)
	endif()

	foreach(test_src IN LISTS TEST_SOURCES)
		file(RELATIVE_PATH relpath "${CMAKE_CURRENT_SOURCE_DIR}" "${test_src}")
		string(REPLACE "\\" "/" relpath "${relpath}")
		get_filename_component(rel_dir "${relpath}" DIRECTORY)
		get_filename_component(test_name "${relpath}" NAME_WE)

		if (NOT rel_dir STREQUAL "")
			set(test_case "${rel_dir}/${test_name}")
		else()
			set(test_case "${test_name}")
		endif()

		add_test(NAME ${test_case} COMMAND toast_tests --test ${test_case})
		if (NOT rel_dir STREQUAL "")
			set_tests_properties(${test_case} PROPERTIES LABELS "${rel_dir}")
		endif()
	endforeach()
endif()