cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0074 NEW)

project(vNerveBliveReceptor)
set(CMAKE_CXX_STANDARD 17)

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
    message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
    file(DOWNLOAD "https://github.com/conan-io/cmake-conan/raw/master/conan.cmake" "${CMAKE_BINARY_DIR}/conan.cmake")
endif()
include(${CMAKE_BINARY_DIR}/conan.cmake)
conan_check(REQUIRED)

add_executable(vnerve_bilive_receptor
    src/main.cpp
    src/config.cpp
    src/bili_session.cpp
    src/bili_conn.cpp
    src/bili_packet.cpp)
target_include_directories(vnerve_bilive_receptor PUBLIC src)

conan_cmake_run(REQUIRES "boost/1.72.0" "nlohmann_json/3.7.3" "zlib/1.2.11"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

if (WIN32)
    target_compile_definitions(vnerve_bilive_receptor PUBLIC "-D_WIN32_WINNT=0x0601")
endif()
target_link_libraries(vnerve_bilive_receptor CONAN_PKG::nlohmann_json CONAN_PKG::boost)