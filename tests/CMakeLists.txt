cmake_minimum_required(VERSION 3.20)
project(Tests)

add_executable(Test)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cc")
target_sources(Test PRIVATE ${TEST_SOURCES})

find_package(GTest REQUIRED)
target_link_libraries(Test GTest::GTest GTest::Main
        clownfish
)

include(GoogleTest)
gtest_discover_tests(Test)
