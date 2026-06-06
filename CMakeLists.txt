cmake_minimum_required(VERSION 3.16)
project(i486_emulator_cpp VERSION 0.2.0 LANGUAGES CXX)

add_library(i486_emulator
    src/assembler.cpp
    src/bus.cpp
    src/compiler.cpp
    src/cpu.cpp
    src/devices.cpp
    src/emulator.cpp
    src/memory.cpp
)
target_include_directories(i486_emulator PUBLIC include)
target_compile_features(i486_emulator PUBLIC cxx_std_17)

add_executable(i486_tests tests/i486_tests.cpp)
target_link_libraries(i486_tests PRIVATE i486_emulator)

add_executable(c486tool src/toolchain_cli.cpp)
target_link_libraries(c486tool PRIVATE i486_emulator)

enable_testing()
add_test(NAME i486_tests COMMAND i486_tests)
