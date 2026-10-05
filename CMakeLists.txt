cmake_minimum_required(VERSION 3.16)
project(king C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

add_library(kgcore STATIC core/kg_core.c)
target_include_directories(kgcore PUBLIC core)

# Replays a game from a seed and the keyboard polls; tests/difftest.py checks it against the
# original's logs from re/emu/record.py.
add_executable(replay tests/replay.c)
target_link_libraries(replay PRIVATE kgcore)
