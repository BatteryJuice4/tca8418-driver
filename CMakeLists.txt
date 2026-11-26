# tca8418-driver — a small, MCU-independent C driver for the TI TCA8418 keypad scanner and GPIO expander.
cmake_minimum_required(VERSION 3.13)
project(tca8418_driver C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The library: pure C, no platform dependencies. Add it to your firmware and implement the
# tca8418_bus_t I2C callback for your MCU; see README.md and examples/.
add_library(tca8418 STATIC src/tca8418.c)
target_include_directories(tca8418 PUBLIC include)

# Host unit test (registers, event FIFO, GPIOs) against a fake chip. Opt out
# with -DTCA8418_BUILD_TESTS=OFF.
option(TCA8418_BUILD_TESTS "Build the host unit test" ON)
if(TCA8418_BUILD_TESTS)
    enable_testing()
    add_executable(test_tca8418 test/test_tca8418.c)
    target_link_libraries(test_tca8418 PRIVATE tca8418)
    target_include_directories(test_tca8418 PRIVATE test)
    if(NOT MSVC)
        target_compile_options(test_tca8418 PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME test_tca8418 COMMAND test_tca8418)
endif()
