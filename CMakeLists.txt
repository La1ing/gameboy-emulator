cmake_minimum_required(VERSION 3.20)
project(gameboy_emulator CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GB_UBSAN "Build with UndefinedBehaviorSanitizer (trap mode, works with MinGW)" OFF)

# Emulator core, shared by the emulator and the tests
add_library(gb_core STATIC src/main/CPU.cpp)
target_include_directories(gb_core PUBLIC src/main)
if (MSVC)
    target_compile_options(gb_core PUBLIC /W4 /permissive-)
else()
    target_compile_options(gb_core PUBLIC -Wall -Wextra -Wpedantic)
endif()
if (GB_UBSAN)
    target_compile_options(gb_core PUBLIC -fsanitize=undefined -fsanitize-trap=all)
    target_link_options(gb_core PUBLIC -fsanitize=undefined -fsanitize-trap=all)
endif()

add_executable(emulator src/main/main.cpp)
target_link_libraries(emulator PRIVATE gb_core)

# Unit tests (Catch2 main lives in its own file, so it is only compiled once)
add_executable(tests
    src/tests/catch_main.cpp
    src/tests/test_block0.cpp
    src/tests/test_ld.cpp
    src/tests/test_alu.cpp
    src/tests/test_control.cpp
    src/tests/test_cb.cpp
    src/tests/test_sweep.cpp
)
target_link_libraries(tests PRIVATE gb_core)

enable_testing()
add_test(NAME opcode_tests COMMAND tests)
