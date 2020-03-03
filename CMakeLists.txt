cmake_minimum_required(VERSION 3.14)
cmake_policy(SET CMP0074 NEW)
set(CMAKE_CXX_STANDARD 17)

project(vNerveBiliveReceptor)

include(conan.cmake)
conan_check(REQUIRED)

add_executable(vnerve_bilive_receptor main.cpp)

conan_cmake_run(REQUIRES "boost/1.72.0" "nlohmann_json/3.7.3"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

target_link_libraries(vnerve_bilive_receptor CONAN_PKG::nlohmann_json CONAN_PKG::boost)