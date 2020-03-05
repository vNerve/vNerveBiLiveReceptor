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
conan_cmake_run(REQUIRES "boost/1.72.0" "nlohmann_json/3.7.3" "protobuf/3.9.1@bincrafters/stable"
                BASIC_SETUP CMAKE_TARGETS
                BUILD missing)

aux_source_directory(src SOURCE_FILES)

add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${COMPILED_PROTO_FILES})

target_link_libraries(${PROJECT_NAME} CONAN_PKG:boost CONAN_PKG::nlohmann_json CONAN_PKG::protobuf)
