cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0074 NEW)

project(vNerveBliveReceptor)
set(CMAKE_CXX_STANDARD 17)

include(${CMAKE_BINARY_DIR}/conan.cmake)
conan_check(REQUIRED)

add_executable(vnerve_bilive_receptor
    src_worker/main.cpp
    src_shared/config.cpp
    src_worker/config.cpp
    src_worker/bili_session.cpp
    src_worker/bili_conn.cpp
    src_worker/bili_packet.cpp)
target_include_directories(vnerve_bilive_receptor PUBLIC src_worker src_shared)

add_executable(vnerve_bilive_receptor_supervisor
    src_supervisor/main.cpp
    src_shared/config.cpp
    src_supervisor/config.cpp
    src_supervisor/grpc_updater.cpp)
target_include_directories(vnerve_bilive_receptor_supervisor PUBLIC src_supervisor src_shared)

conan_cmake_run(REQUIRES "boost/1.72.0" "zlib/1.2.11" "spdlog/1.5.0"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

if (WIN32)
    target_compile_definitions(vnerve_bilive_receptor            PUBLIC "-D_WIN32_WINNT=0x0601")
    target_compile_definitions(vnerve_bilive_receptor_supervisor PUBLIC "-D_WIN32_WINNT=0x0601")
endif()
target_link_libraries(vnerve_bilive_receptor            CONAN_PKG::boost CONAN_PKG::zlib CONAN_PKG::spdlog)
target_link_libraries(vnerve_bilive_receptor_supervisor CONAN_PKG::boost CONAN_PKG::zlib CONAN_PKG::spdlog)