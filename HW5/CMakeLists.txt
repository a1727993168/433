cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(HW5 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(HW5
    HW5.c
    ssd1306.c
)

target_link_libraries(HW5
    pico_stdlib
    hardware_i2c
)

pico_enable_stdio_usb(HW5 1)
pico_enable_stdio_uart(HW5 0)

pico_add_extra_outputs(HW5)