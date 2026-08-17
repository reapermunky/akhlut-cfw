cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(freewili_display C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(freewili_display
    src/main.c
    src/gfx.c
    src/ui.c
    src/ir_driver.c
    ../common/ipp.c
)

target_include_directories(freewili_display PRIVATE
    include
    ../common
)

target_link_libraries(freewili_display
    pico_stdlib
    pico_multicore
    hardware_uart
    hardware_spi
    hardware_i2c
    hardware_gpio
    hardware_pwm
    hardware_timer
    hardware_pio
)

# Display processor does NOT use USB CDC
pico_enable_stdio_usb(freewili_display 0)
pico_enable_stdio_uart(freewili_display 0)

pico_generate_pio_header(freewili_display ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

pico_add_extra_outputs(freewili_display)
