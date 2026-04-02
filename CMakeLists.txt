cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file"
    )
endif()

project(simpleDBMS-Server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SERVER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
find_package(asio CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(SERVER_VCPKG_TARGETS
    asio::asio
    nlohmann_json::nlohmann_json
)

file(GLOB_RECURSE SERVER_SOURCE_FILES CONFIGURE_DEPENDS
    "${SERVER_SOURCE_DIR}/*.cpp"
    "${SERVER_SOURCE_DIR}/*.h"
)

add_executable(simpleDBMS-Server
    ${SERVER_SOURCE_FILES}
)

target_include_directories(simpleDBMS-Server
    PRIVATE
        ${SERVER_SOURCE_DIR}
)

target_link_libraries(simpleDBMS-Server
    PRIVATE
        ${SERVER_VCPKG_TARGETS}
)
