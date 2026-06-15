cmake_minimum_required(VERSION 3.16)
project(minispire CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(minispire
  src/combat_state.cc
  src/enemy.cc
  src/turn_loop.cc
)

target_include_directories(minispire PUBLIC src)

add_executable(minispire-cli src/cli.cc)
target_link_libraries(minispire-cli PRIVATE minispire)

# Tests
option(MINISPIRE_BUILD_TESTS "Build unit tests" ON)
if(MINISPIRE_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
  )
  # Avoid GoogleTest installing itself / overriding parent project settings.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()

  add_executable(minispire_tests
    tests/test_helpers.cc
    tests/test_card.cc
    tests/test_enemy.cc
    tests/test_combat_state.cc
    tests/test_turn_loop.cc
  )
  target_link_libraries(minispire_tests PRIVATE minispire GTest::gtest_main)
  target_include_directories(minispire_tests PRIVATE tests)

  include(GoogleTest)
  gtest_discover_tests(minispire_tests)
endif()
