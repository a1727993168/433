cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== Pico SDK 自动路径（VSCode插件用）=====
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

# ===== 板子 =====
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# ===== 引入 SDK =====
include(pico_sdk_import.cmake)

project(hw6 C CXX ASM)

pico_sdk_init()

# ===== 可执行文件 =====
add_executable(hw6
    main.c
    usb_descriptors.c
)

# ===== include =====
target_include_directories(hw6 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# ===== 关键：库 =====
target_link_libraries(hw6 PUBLIC
    pico_stdlib
    tinyusb_device
    tinyusb_board
    hardware_gpio
    hardware_i2c
)

# ===== 输出 UF2 =====
pico_add_extra_outputs(hw6)
