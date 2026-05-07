cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file"
    )
endif()

project(simpleDBMS-Server LANGUAGES CXX)

if(MSVC)
    add_compile_options(/utf-8)
endif()

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

list(FILTER SERVER_SOURCE_FILES EXCLUDE REGEX ".*/src/storage/tests/.*\\.cpp$")
list(FILTER SERVER_SOURCE_FILES EXCLUDE REGEX ".*/src/test/.*\\.cpp$")
list(FILTER SERVER_SOURCE_FILES EXCLUDE REGEX ".*/src/storage/object/StorageDemo\\.cpp$")

add_executable(simpleDBMS-Server
    ${SERVER_SOURCE_FILES}
)

target_include_directories(simpleDBMS-Server
    PRIVATE
        ${SERVER_SOURCE_DIR}
)

if(WIN32)
    target_compile_definitions(simpleDBMS-Server
        PRIVATE
            _WIN32_WINNT=0x0601
    )
endif()

target_link_libraries(simpleDBMS-Server
    PRIVATE
        ${SERVER_VCPKG_TARGETS}
)

set(SERVER_TEST_SOURCE_FILES ${SERVER_SOURCE_FILES})
list(FILTER SERVER_TEST_SOURCE_FILES EXCLUDE REGEX ".*/src/main\\.cpp$")

file(GLOB SERVER_TEST_FILES CONFIGURE_DEPENDS
    "${SERVER_SOURCE_DIR}/test/*Test.cpp"
)

foreach(TEST_FILE ${SERVER_TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    set(TEST_TARGET "simpleDBMS-Server-${TEST_NAME}")

    add_executable(${TEST_TARGET}
        ${SERVER_TEST_SOURCE_FILES}
        ${TEST_FILE}
    )

    target_include_directories(${TEST_TARGET}
        PRIVATE
            ${SERVER_SOURCE_DIR}
    )

    if(WIN32)
        target_compile_definitions(${TEST_TARGET}
            PRIVATE
                _WIN32_WINNT=0x0601
                SERVER_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()

    target_link_libraries(${TEST_TARGET}
        PRIVATE
            ${SERVER_VCPKG_TARGETS}
    )
endforeach()
