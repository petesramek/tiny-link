cmake_minimum_required(VERSION 3.16)

project(tiny_link LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

enable_testing()

file(GLOB_RECURSE TINYLINK_SRC CONFIGURE_DEPENDS
    src/*.cpp
)

add_executable(tiny_link_tests
    test/test_main.cpp
    ${TINYLINK_SRC}
)

target_include_directories(tiny_link_tests PRIVATE
    src
    test
)

target_compile_definitions(tiny_link_tests PRIVATE
    TINYLINK_NATIVE_TEST
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(tiny_link_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Debug>:-O0;-g3>
    )
endif()

add_test(NAME tiny_link_tests COMMAND tiny_link_tests)