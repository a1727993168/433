# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Pico 2W -> RP2350, wireless variant
set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(hw14 C CXX ASM)

pico_sdk_init()

add_executable(hw14
    main.c
)

pico_set_program_name(hw14 "hw14")
pico_set_program_version(hw14 "0.1")

# Use USB CDC for stdio (so the Python script talks over the USB cable),
# turn off the UART so it doesn't fight for the same stdio.
pico_enable_stdio_usb(hw14 1)
pico_enable_stdio_uart(hw14 0)

target_link_libraries(hw14 pico_stdlib)

pico_add_extra_outputs(hw14)
