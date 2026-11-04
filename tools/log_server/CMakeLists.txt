project(log_server CXX)

find_package(protobuf CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(LOG_SERVER_PROTO "${CMAKE_SOURCE_DIR}/protos/logging.proto")
set(LOG_SERVER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(LOG_SERVER_PROTO_SOURCES "${LOG_SERVER_GENERATED_DIR}/logging.pb.cc" "${LOG_SERVER_GENERATED_DIR}/logging.pb.h")

add_custom_command(
        OUTPUT ${LOG_SERVER_PROTO_SOURCES}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${LOG_SERVER_GENERATED_DIR}"
        COMMAND $<TARGET_FILE:protobuf::protoc>
            "--proto_path=${CMAKE_SOURCE_DIR}/protos"
            "--cpp_out=${LOG_SERVER_GENERATED_DIR}"
            "${LOG_SERVER_PROTO}"
        DEPENDS "${LOG_SERVER_PROTO}"
        COMMENT "[protoc] Generating log_server protobuf sources..."
        VERBATIM
)

set(LOG_SERVER_CORE_SOURCES
        src/server.cpp
        src/storage.cpp
        src/upload.cpp
)
set(LOG_SERVER_SOURCES src/main.cpp ${LOG_SERVER_CORE_SOURCES})

add_executable(log_server ${LOG_SERVER_SOURCES} ${LOG_SERVER_PROTO_SOURCES})
target_include_directories(log_server PRIVATE src "${LOG_SERVER_GENERATED_DIR}")
target_link_libraries(log_server PRIVATE protobuf::libprotobuf asio::asio Threads::Threads CURL::libcurl ZLIB::ZLIB nlohmann_json::nlohmann_json)
target_compile_definitions(log_server PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED)

if (MSVC)
    target_compile_definitions(log_server PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN NOGDI _CRT_SECURE_NO_WARNINGS)
    target_link_options(log_server PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    set_source_files_properties(${LOG_SERVER_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8")
else ()
    set_source_files_properties(${LOG_SERVER_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif ()

foreach (config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(log_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}/log_server")
endforeach ()

add_executable(log_server_checks EXCLUDE_FROM_ALL checks/checks.cpp ${LOG_SERVER_CORE_SOURCES} ${LOG_SERVER_PROTO_SOURCES})
target_include_directories(log_server_checks PRIVATE src "${LOG_SERVER_GENERATED_DIR}")
target_link_libraries(log_server_checks PRIVATE protobuf::libprotobuf asio::asio Threads::Threads CURL::libcurl ZLIB::ZLIB nlohmann_json::nlohmann_json)
target_compile_definitions(log_server_checks PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED)
if (MSVC)
    target_compile_definitions(log_server_checks PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN NOGDI _CRT_SECURE_NO_WARNINGS)
    set_source_files_properties(checks/checks.cpp PROPERTIES COMPILE_OPTIONS "/W4;/utf-8")
else ()
    set_source_files_properties(checks/checks.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif ()
foreach (config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(log_server_checks PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${OUTPUT_ROOT}/${config}/log_server")
endforeach ()
