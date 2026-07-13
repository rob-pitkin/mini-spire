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
  src/combat_env.cc
  src/enemy.cc
  src/encounter.cc
  src/turn_loop.cc
)

target_include_directories(minispire PUBLIC src)

# Position-independent code so this static lib can be linked into the
# pybind11 shared-object extension module (required on Linux; harmless on
# macOS/Windows).
set_target_properties(minispire PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Pybind11 extension module — only when scikit-build-core invokes us.
if(SKBUILD)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(_core bindings/_core.cc)
  target_link_libraries(_core PRIVATE minispire)
  # Install into the package directory inside the wheel.
  install(TARGETS _core LIBRARY DESTINATION minispire)
endif()

# Tests
option(MINISPIRE_BUILD_TESTS "Build unit tests" ON)
if(MINISPIRE_BUILD_TESTS AND NOT SKBUILD)
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
    tests/test_combat_env.cc
    tests/test_encounter.cc
  )
  target_link_libraries(minispire_tests PRIVATE minispire GTest::gtest_main)
  target_include_directories(minispire_tests PRIVATE tests)

  include(GoogleTest)
  gtest_discover_tests(minispire_tests)
endif()
