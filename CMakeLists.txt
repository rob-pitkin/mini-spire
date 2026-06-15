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
