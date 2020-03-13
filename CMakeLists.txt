cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0074 NEW)
set(CMAKE_CXX_STANDARD 17)

project(vNerveBiliveReceptor)

include(conan.cmake)
conan_check(REQUIRED)

add_executable(vnerve_bilive_receptor main.cpp config.cpp bili_session.cpp bili_conn.cpp bili_packet.cpp)

conan_cmake_run(REQUIRES "boost/1.72.0" "nlohmann_json/3.7.3" "zlib/1.2.11"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

if (WIN32)
    target_compile_definitions(vnerve_bilive_receptor PUBLIC "-D_WIN32_WINNT=0x0601")
endif()
target_link_libraries(vnerve_bilive_receptor CONAN_PKG::nlohmann_json CONAN_PKG::boost)