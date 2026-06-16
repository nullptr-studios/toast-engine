macro(run_codegen)
    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/engine/generated")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/engine/generated")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/engine/assets")

    # Protobuf
    find_package(protobuf CONFIG REQUIRED)

    get_target_property(_protoc protobuf::protoc IMPORTED_LOCATION_RELEASE)
    if(NOT _protoc)
        get_target_property(_protoc protobuf::protoc IMPORTED_LOCATION_DEBUG)
    endif()
    if(NOT _protoc)
        get_target_property(_protoc protobuf::protoc IMPORTED_LOCATION)
    endif()
    if(NOT _protoc)
        find_program(_protoc protoc REQUIRED)
    endif()

    message(STATUS "Using protoc: ${_protoc}")

    file(GLOB _proto_files "${CMAKE_SOURCE_DIR}/protos/*.proto")
    foreach(_proto ${_proto_files})
        execute_process(
            COMMAND "${_protoc}"
                --proto_path=${CMAKE_SOURCE_DIR}/protos
                --cpp_out=${CMAKE_SOURCE_DIR}/engine/generated
                ${_proto}
            RESULT_VARIABLE _protoc_result
            ERROR_VARIABLE  _protoc_error
        )
        if(NOT _protoc_result EQUAL 0)
            message(FATAL_ERROR "protoc failed for ${_proto}:\n${_protoc_error}")
        endif()
    endforeach()
    message(STATUS "Protobuf sources generated")

    message(STATUS "Building reflection_generator tool...")

    set(_cfg "${CMAKE_BUILD_TYPE}")
    if(NOT _cfg)
        set(_cfg "Debug")
    endif()
    set(_refgen_cargo_dir "${OUTPUT_ROOT}/${_cfg}/cargo")
    set(_refgen_stage_dir "${OUTPUT_ROOT}/${_cfg}/reflection_generator")

    find_program(CARGO cargo REQUIRED)
    execute_process(
        COMMAND ${CARGO} build --target-dir "${_refgen_cargo_dir}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/reflection_generator"
        RESULT_VARIABLE _cargo_result
        ERROR_VARIABLE  _cargo_error
    )
    if(NOT _cargo_result EQUAL 0)
        message(FATAL_ERROR "Failed to build reflection_generator:\n${_cargo_error}")
    endif()

    file(MAKE_DIRECTORY "${_refgen_stage_dir}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_refgen_cargo_dir}/debug"
            "${_refgen_stage_dir}"
        RESULT_VARIABLE _stage_result
        ERROR_VARIABLE  _stage_error
    )
    if(NOT _stage_result EQUAL 0)
        message(FATAL_ERROR "Failed to stage reflection_generator:\n${_stage_error}")
    endif()

    set(REFLECTION_GENERATOR "${_refgen_stage_dir}/reflection_generator${CMAKE_EXECUTABLE_SUFFIX}")

    message(STATUS "Using reflection_generator: ${REFLECTION_GENERATOR}")
    execute_process(
        COMMAND ${REFLECTION_GENERATOR}
            --database "${CMAKE_SOURCE_DIR}/engine/assets/engine_reflect.json"
            --output   "${CMAKE_SOURCE_DIR}/engine/generated"
            --input    "${CMAKE_SOURCE_DIR}/engine/src/toast"
            --include-root "${CMAKE_SOURCE_DIR}/engine/src"
            --register-fn "registerEngineTypes"
        RESULT_VARIABLE _refgen_result
        OUTPUT_VARIABLE _refgen_output
        ERROR_VARIABLE  _refgen_error
    )
    if(NOT _refgen_result EQUAL 0)
        message(FATAL_ERROR "Reflection generation failed:\n${_refgen_error}")
    else()
        message(STATUS "Reflection metadata generated successfully")
        message(STATUS "${_refgen_output}")
    endif()
endmacro()
