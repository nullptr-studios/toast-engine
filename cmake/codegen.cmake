macro(run_codegen)

    file(LOCK "${OUTPUT_ROOT}/.codegen.lock" GUARD PROCESS)
    set(_gen_dir "${CMAKE_SOURCE_DIR}/engine/generated")
    set(_gen_old "${CMAKE_SOURCE_DIR}/engine/generated.old")
    file(REMOVE_RECURSE "${_gen_old}")
    if(EXISTS "${_gen_dir}")
        file(RENAME "${_gen_dir}" "${_gen_old}")
    endif()
    file(MAKE_DIRECTORY "${_gen_dir}")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/engine/assets")

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
    if(_cfg STREQUAL "Release" OR _cfg STREQUAL "RelWithDebInfo" OR _cfg STREQUAL "MinSizeRel")
        set(_dotnet_config "Release")
    else()
        set(_dotnet_config "Debug")
    endif()
    set(_refgen_dir "${OUTPUT_ROOT}/${_cfg}/reflection_generator")

    find_program(DOTNET dotnet REQUIRED)
    execute_process(
        COMMAND ${DOTNET} msbuild -restore "${CMAKE_SOURCE_DIR}/tools/reflection_generator/reflection_generator.csproj"
            -property:Configuration=${_dotnet_config} "-property:OutputPath=${_refgen_dir}/" -property:_CommandLineDefinedOutputPath=true -nologo -verbosity:quiet -consoleloggerparameters:NoSummary
        RESULT_VARIABLE _dotnet_result
        OUTPUT_VARIABLE _dotnet_output
        ERROR_VARIABLE  _dotnet_error
    )
    if(NOT _dotnet_result EQUAL 0)
        # msbuild reports compile errors on stdout
        message(FATAL_ERROR "Failed to build reflection_generator:\n${_dotnet_output}\n${_dotnet_error}")
    endif()

    set(REFLECTION_GENERATOR "${_refgen_dir}/reflection_generator${CMAKE_EXECUTABLE_SUFFIX}")
    if(NOT EXISTS "${REFLECTION_GENERATOR}")
        message(FATAL_ERROR "reflection_generator binary not found at ${REFLECTION_GENERATOR}")
    endif()

    message(STATUS "Using reflection_generator: ${REFLECTION_GENERATOR}")
    execute_process(
        COMMAND ${REFLECTION_GENERATOR}
            --database "${CMAKE_SOURCE_DIR}/engine/assets/engine_reflect.json"
            --output   "${CMAKE_SOURCE_DIR}/engine/generated"
            --input    "${CMAKE_SOURCE_DIR}/engine/src/toast"
            --include-root "${CMAKE_SOURCE_DIR}/engine/src"
            --register-fn "registerEngineTypes"
            --split-typeinfo
            --lua-stubs "${CMAKE_SOURCE_DIR}/engine/assets/lua/types.d.lua"
            --event-lua-stubs "${CMAKE_SOURCE_DIR}/engine/assets/lua/events.d.lua"
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

    # Identical output gets its old file and mtime back so nothing downstream rebuilds
    file(GLOB _gen_new RELATIVE "${_gen_dir}" "${_gen_dir}/*")
    foreach(_f ${_gen_new})
        if(EXISTS "${_gen_old}/${_f}")
            file(SHA1 "${_gen_old}/${_f}" _h_old)
            file(SHA1 "${_gen_dir}/${_f}" _h_new)
            if(_h_old STREQUAL _h_new)
                file(COPY "${_gen_old}/${_f}" DESTINATION "${_gen_dir}")
            endif()
        endif()
    endforeach()
    file(REMOVE_RECURSE "${_gen_old}")
endmacro()
