cmake_minimum_required(VERSION 3.15)

project(Rapt0R LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

file(GLOB_RECURSE SOURCE_FILES
    Raptor/*.cpp
    Raptor/*.hpp 
)

set(INCLUDE_DIRS
    Raptor/Source
    Raptor/Include
)

set(LIB_LINKS)

add_executable(RaptorTest ${SOURCE_FILES})
target_include_directories(RaptorTest PUBLIC ${INCLUDE_DIRS})
target_link_libraries(RaptorTest PUBLIC ${LIB_LINKS})
