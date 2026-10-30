set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

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

# Signal Generator Rev C: Raspberry Pi Pico 2 W (RP2350) driving DAC8830.
set(PICO_BOARD pico2_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (must be set before project())
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico2w_dds C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico2w_dds
    main.c
    dds_shared.c
    spi_dac.c
    dds_tables.c
    i2c_slave.c
)

target_include_directories(pico2w_dds PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pico2w_dds
    pico_stdlib
    pico_multicore
    pico_i2c_slave
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_spi
    hardware_gpio
    hardware_i2c
    hardware_watchdog 
)
pico_generate_pio_header(pico2w_dds ${CMAKE_CURRENT_LIST_DIR}/dac_sync.pio)

# Enable USB serial for debugging; disable UART serial
pico_enable_stdio_usb(pico2w_dds 1)
pico_enable_stdio_uart(pico2w_dds 0)

# Generate UF2, bin, hex outputs
pico_add_extra_outputs(pico2w_dds)