cmake_minimum_required(VERSION 3.20)
project(Examples)

add_executable(Examples)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")
target_sources(Examples PRIVATE ${TEST_SOURCES})
target_compile_features(Examples PRIVATE cxx_std_17)

target_link_libraries(Examples PRIVATE clownfish)