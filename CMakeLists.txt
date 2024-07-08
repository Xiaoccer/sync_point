cmake_minimum_required(VERSION 3.14)
project(sync_point)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_compile_definitions(UNIT_TEST)

add_executable(
  sync_point_test
  sync_point_test.cc
  sync_point.cc
  sync_point.h
)
target_link_libraries(
  sync_point_test
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(sync_point_test)
