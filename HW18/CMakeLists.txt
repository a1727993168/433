# ===========================================================================
#  HW18 Haptic Paddle - Pico SDK build
#  目标板: Raspberry Pi Pico 2 W (RP2350)  -> 需要 Pico SDK >= 2.0
# ===========================================================================
cmake_minimum_required(VERSION 3.13)

# 目标板型号. 用普通 Pico 2 就改成 pico2; 用旧 Pico W 改成 pico_w.
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# 导入 Pico SDK (pico_sdk_import.cmake 见同目录)
include(pico_sdk_import.cmake)

project(haptic_paddle C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(haptic_paddle
    main.c
)

target_link_libraries(haptic_paddle
    pico_stdlib
    hardware_i2c
    hardware_pwm
    hardware_gpio
)

# 用 USB 当 printf 串口 (关掉 UART)
pico_enable_stdio_usb(haptic_paddle 1)
pico_enable_stdio_uart(haptic_paddle 0)

# 生成 .uf2 等烧录文件
pico_add_extra_outputs(haptic_paddle)
