cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0074 NEW)

project(vNerveBiLiveReceptor)
set(CMAKE_CXX_STANDARD 17)

include(${CMAKE_BINARY_DIR}/conan.cmake)
conan_check(REQUIRED)

set(EXECUTABLE_NAME
    v_nerve_bilibili_receptor)
set(SOURCE_FILES
    src/main.cpp
    src/config.cpp
    src/bili_session.cpp
    src/bili_conn.cpp
    src/bili_packet.cpp)
set(COMPILED_PROTO_FILES
    proto/cpp/vNerve/bilibili/live/message.pb.cc)

add_executable(${EXECUTABLE_NAME} ${SOURCE_FILES} ${COMPILED_PROTO_FILES})
target_include_directories(
    ${EXECUTABLE_NAME} PUBLIC
    src
    proto/cpp)

conan_cmake_run(REQUIRES
                    "boost/1.72.0"
                    "zlib/1.2.11"
                    "spdlog/1.5.0"
                    "rapidjson/1.1.0"
                    "protobuf/3.11.4@charliejiang/stable"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

if (WIN32)
    target_compile_definitions(${EXECUTABLE_NAME} PUBLIC "-D_WIN32_WINNT=0x0601")
endif()
target_link_libraries(${EXECUTABLE_NAME}
                        CONAN_PKG::boost
                        CONAN_PKG::zlib
                        CONAN_PKG::spdlog
                        CONAN_PKG::rapidjson
                        CONAN_PKG::protobuf)
