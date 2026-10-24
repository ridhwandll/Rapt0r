cmake_minimum_required(VERSION 3.15)

project(Rapt0R LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(Rapt0R INTERFACE)
add_library(Rapt0R::Rapt0R ALIAS Rapt0R)
target_include_directories(Rapt0R INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Raptor/Include)
target_compile_features(Rapt0R INTERFACE cxx_std_20)

function(rapt0r_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

file(GLOB_RECURSE SOURCE_FILES
    Raptor/*.cpp
    Raptor/*.hpp
)

add_executable(RaptorTest ${SOURCE_FILES})
target_link_libraries(RaptorTest PRIVATE Rapt0R)
rapt0r_set_warnings(RaptorTest)

add_executable(RaptorUnitTests Tests/Tests.cpp)
target_link_libraries(RaptorUnitTests PRIVATE Rapt0R)
rapt0r_set_warnings(RaptorUnitTests)

enable_testing()
add_test(NAME RaptorUnitTests COMMAND RaptorUnitTests)

option(RAPTOR_BUILD_COMPETITOR_BENCHMARKS "Download Google Benchmark, zpp_bits, bitsery, alpaca, cereal and glaze, and build a head-to-head benchmark" OFF)
if(RAPTOR_BUILD_COMPETITOR_BENCHMARKS)
    add_subdirectory(Benchmarks)
endif()
