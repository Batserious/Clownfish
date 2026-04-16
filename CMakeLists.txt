cmake_minimum_required(VERSION 3.16)

set(Clownfish_VERSION "1.0.0")
set(PACKAGE_VERSION ${Clownfish_VERSION})

project("Clownfish" VERSION ${PACKAGE_VERSION})

set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_VERBOSE_MAKEFILE ON)

message(STATUS "Configuring ${PROJECT_NAME} ${Clownfish_VERSION}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(CTest)

### Main Build Targets
set(Clownfish_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_library(clownfish INTERFACE)
add_library(Clownfish::Clownfish ALIAS clownfish)

target_sources(clownfish INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/Clownfish/Clownfish.hpp>)

target_include_directories(clownfish INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Tests
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(tests)
endif ()

option(BUILD_EXAMPLES "Build code examples" ON)
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()
