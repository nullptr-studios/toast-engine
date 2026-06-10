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
    find_program(CARGO cargo REQUIRED)
    execute_process(
        COMMAND ${CARGO} build
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/reflection_generator"
        RESULT_VARIABLE _cargo_result
        ERROR_VARIABLE  _cargo_error
    )
    if(NOT _cargo_result EQUAL 0)
        message(FATAL_ERROR "Failed to build reflection_generator:\n${_cargo_error}")
    endif()
    find_program(REFLECTION_GENERATOR reflection_generator
        PATHS "${CMAKE_SOURCE_DIR}/tools/reflection_generator/target/debug"
        NO_DEFAULT_PATH REQUIRED
    )

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
