include(CTest)

if (BUILD_TESTING)
	file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
	list(REMOVE_ITEM TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_runner.cpp")

	add_executable(toast_tests ${TEST_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/test_runner.cpp" "${CMAKE_SOURCE_DIR}/engine/src/toast/assets/node_file.cpp")
	target_include_directories(toast_tests BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
	target_include_directories(toast_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(toast_tests PRIVATE toast_engine)
	target_compile_definitions(toast_tests PRIVATE UNIT_TESTING TRACY_NO_INVARIANT_CHECK=1)


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
