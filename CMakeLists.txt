cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0074 NEW)

project(vNerveBiLiveReceptor)
set(CMAKE_CXX_STANDARD 17)

include(conan.cmake)
conan_check(REQUIRED)

conan_cmake_run(REQUIRES
                    "boost/1.72.0"
                    "zlib/1.2.11"
                    "spdlog/1.5.0"
                    "rapidjson/1.1.0"
                    "protobuf/3.11.4@charliejiang/stable"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

set(WORKER_EXECUTABLE_NAME
    v_nerve_bilibili_receptor)
set(WORKER_SOURCE_FILES
    src/shared/config.cpp

    src/worker/main.cpp
    src/worker/config.cpp
    src/worker/bili_session.cpp
    src/worker/bili_conn.cpp
    src/worker/bili_packet.cpp
    src/worker/supervisor_connection.cpp
    src/worker/supervisor_session.cpp

    proto/cpp/vNerve/bilibili/live/message.pb.cc
    proto/cpp/vNerve/bilibili/live/room_message.pb.cc
    proto/cpp/vNerve/bilibili/live/user_message.pb.cc)

add_executable(${WORKER_EXECUTABLE_NAME} ${WORKER_SOURCE_FILES})
target_include_directories(
    ${WORKER_EXECUTABLE_NAME} PUBLIC
    vendor
    src/worker
    src/shared
    vendor/
    proto/cpp)

set(SUPERVISOR_EXECUTABLE_NAME
    v_nerve_bilibili_receptor_supervisor)
set(SUPERVISOR_SOURCE_FILES
    src/shared/config.cpp

    src/supervisor/main.cpp
    src/supervisor/config.cpp
    src/supervisor/room_list_updater.cpp
    src/supervisor/supervisor_server_session.cpp
    src/supervisor/worker_scheduler.cpp
    )
add_executable(${SUPERVISOR_EXECUTABLE_NAME} ${SUPERVISOR_SOURCE_FILES})
target_include_directories(
    ${SUPERVISOR_EXECUTABLE_NAME} PUBLIC
    src/third_party
    src/suprtvisor
    src/shared
    proto/cpp)

conan_cmake_run(REQUIRES
                    "boost/1.72.0"
                    "zlib/1.2.11"
                    "spdlog/1.5.0"
                    "rapidjson/1.1.0"
                    "protobuf/3.11.4@charliejiang/stable"
                    "cpp-httplib/0.5.7"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

if (WIN32)
    target_compile_definitions(${WORKER_EXECUTABLE_NAME} PUBLIC "-D_WIN32_WINNT=0x0601")
    target_compile_definitions(${SUPERVISOR_EXECUTABLE_NAME} PUBLIC "-D_WIN32_WINNT=0x0601")
    target_compile_options(${WORKER_EXECUTABLE_NAME} PUBLIC "/utf-8")
endif()
target_link_libraries(${WORKER_EXECUTABLE_NAME}
                        CONAN_PKG::boost
                        CONAN_PKG::zlib
                        CONAN_PKG::spdlog
                        CONAN_PKG::rapidjson
                        CONAN_PKG::protobuf
                        )
target_link_libraries(${SUPERVISOR_EXECUTABLE_NAME}
                        CONAN_PKG::boost
                        CONAN_PKG::spdlog
                        CONAN_PKG::protobuf
                        CONAN_PKG::rapidjson
                        CONAN_PKG::cpp-httplib)

if (WIN32)
    target_compile_definitions(${WORKER_EXECUTABLE_NAME} PUBLIC "-D_WIN32_WINNT=0x0601")
    target_compile_options(${WORKER_EXECUTABLE_NAME} PUBLIC "/utf-8")
endif()


# set(SUPERVISOR_EXECUTABLE_NAME
#     v_nerve_bilibili_receptor_supervisor)
# set(SUPERVISOR_SOURCE_FILES
#     src/shared/config.cpp

#     src/supervisor/main.cpp
#     src/supervisor/config.cpp)
# add_executable(${SUPERVISOR_EXECUTABLE_NAME} ${SUPERVISOR_SOURCE_FILES})
# target_include_directories(
#     ${SUPERVISOR_EXECUTABLE_NAME} PUBLIC
#     src/supervisor
#     src/shared
#     proto/cpp)
# target_link_libraries(${SUPERVISOR_EXECUTABLE_NAME}
#                         CONAN_PKG::boost
#                         CONAN_PKG::spdlog)

# if (WIN32)
#     target_compile_definitions(${SUPERVISOR_EXECUTABLE_NAME} PUBLIC "-D_WIN32_WINNT=0x0601")
#     target_compile_options(${SUPERVISOR_EXECUTABLE_NAME} PUBLIC "/utf-8")
# endif()
