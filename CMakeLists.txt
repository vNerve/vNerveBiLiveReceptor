cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0074 NEW)

project(vNerveBiLiveReceptor)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include(conan.cmake)
conan_check(REQUIRED)

set(WORKER_EXECUTABLE_NAME
    v_nerve_bilibili_receptor)
set(WORKER_SOURCE_FILES
    "src/shared/config.cpp"
    "src/shared/simple_worker_proto.cpp"
    "src/shared/simple_worker_proto_handler.cpp"
    "src/shared/asio_socket_write_helper.cpp"
    "src/shared/http_interval_updater.cpp"
    "src/shared/windows_minidump.cpp"

    "src/worker/main.cpp"
    "src/worker/config.cpp"
    "src/worker/bilibili_token_updater.cpp"
    "src/worker/bilibili_connection_manager.cpp"
    "src/worker/bilibili_live_config.cpp"
    "src/worker/bili_conn_plain_tcp.cpp"
    "src/worker/bili_conn_ws.cpp"
    "src/worker/bili_packet.cpp"
    "src/worker/bili_json.cpp"
    "src/worker/supervisor_connection.cpp"
    "src/worker/supervisor_session.cpp"
    "src/worker/simple_worker_proto_generator.cpp"
    "src/worker/global_context.cpp"

    "proto/cpp/vNerve/bilibili/live/room_message.pb.cc"
    "proto/cpp/vNerve/bilibili/live/user_message.pb.cc")
add_executable(${WORKER_EXECUTABLE_NAME} ${WORKER_SOURCE_FILES})
target_include_directories(
    ${WORKER_EXECUTABLE_NAME} PUBLIC
    src/worker
    src/shared
    vendor/
    proto/cpp)

set(SUPERVISOR_EXECUTABLE_NAME
    v_nerve_bilibili_receptor_supervisor)
set(SUPERVISOR_SOURCE_FILES
    "src/shared/config.cpp"
    "src/shared/simple_worker_proto.cpp"
    "src/shared/simple_worker_proto_handler.cpp"
    "src/shared/asio_socket_write_helper.cpp"
    "src/shared/http_interval_updater.cpp"

    "src/supervisor/main.cpp"
    "src/supervisor/config.cpp"
    "src/supervisor/room_list_updater.cpp"
    "src/supervisor/worker_connection_manager.cpp"
    "src/supervisor/simple_worker_proto_generator.cpp"
    "src/supervisor/worker_scheduler.cpp"
    "src/supervisor/amqp_client.cpp"
    "src/supervisor/deduplicate_context.cpp"
    "src/supervisor/diagnostic_context.cpp"
    "src/supervisor/global_context.cpp"

    "proto/cpp/vNerve/bilibili/live/diagnostics.pb.cc"
)
add_executable(${SUPERVISOR_EXECUTABLE_NAME} ${SUPERVISOR_SOURCE_FILES})
target_include_directories(
    ${SUPERVISOR_EXECUTABLE_NAME} PUBLIC
    vendor
    src/supervisor
    src/shared
    proto/cpp)

set(CONAN_OPTIONS "")
if (WIN32)
    list(APPEND CONAN_OPTIONS "libcurl:with_winssl=True")
    list(APPEND CONAN_OPTIONS "libcurl:with_openssl=False")
endif()
if (UNIX)
    list(APPEND CONAN_OPTIONS "amqp-cpp:linux_tcp_module=False")
endif()

conan_cmake_run(REQUIRES
                    "boost/1.73.0"
                    "zlib/1.2.11"
                    "fmt/6.2.0"
                    "spdlog/1.6.1"
                    "rapidjson/1.1.0"
                    "protobuf/3.11.4"
                    "libcurl/7.70.0"
                    "amqp-cpp/4.1.6"
                    "openssl/1.1.1g"
                    "uriparser/0.9.4"
                BASIC_SETUP CMAKE_TARGETS
                OPTIONS ${CONAN_OPTIONS}
                ENV "CONAN_CMAKE_GENERATOR=Ninja"
                BUILD missing)

if (WIN32)
    target_compile_definitions(${WORKER_EXECUTABLE_NAME} PUBLIC "-D_WIN32_WINNT=0x0601" "-DNOMINMAX")
    target_compile_definitions(${SUPERVISOR_EXECUTABLE_NAME} PUBLIC "-D_WIN32_WINNT=0x0601" "-DNOMINMAX")
    target_compile_options(${WORKER_EXECUTABLE_NAME} PUBLIC "/utf-8")

    set_source_files_properties("src/worker/bili_conn_ws.cpp" PROPERTIES COMPILE_FLAGS "/bigobj")
endif()
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${WORKER_EXECUTABLE_NAME} PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE")
    target_compile_definitions(${SUPERVISOR_EXECUTABLE_NAME} PUBLIC "-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG")
endif()
target_link_libraries(${WORKER_EXECUTABLE_NAME}
                        CONAN_PKG::boost
                        CONAN_PKG::zlib
                        CONAN_PKG::spdlog
                        CONAN_PKG::rapidjson
                        CONAN_PKG::protobuf
                        CONAN_PKG::libcurl
                        CONAN_PKG::openssl
                        CONAN_PKG::uriparser
                        )
target_link_libraries(${SUPERVISOR_EXECUTABLE_NAME}
                        CONAN_PKG::boost
                        CONAN_PKG::spdlog
                        CONAN_PKG::protobuf
                        CONAN_PKG::rapidjson
                        CONAN_PKG::libcurl
                        CONAN_PKG::amqp-cpp
                        )
