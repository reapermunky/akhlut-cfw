cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK (must be set before project())
include(pico_sdk_import.cmake)

project(freewili_main C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(freewili_main
    src/main.c
    src/tool.c
    src/cc1101.c
    src/fs.c
    src/wasm_api.c
    src/tool_subghz.c
    src/tool_ghost.c
    src/tool_bus.c
    src/tool_gpio.c
    src/tool_ir.c
    src/tool_settings.c
    src/tool_about.c
    src/tool_apps.c
    ../common/ipp.c
    lib/littlefs/lfs.c
    lib/littlefs/lfs_util.c
    lib/wasm3/source/m3_core.c
    lib/wasm3/source/m3_env.c
    lib/wasm3/source/m3_compile.c
    lib/wasm3/source/m3_exec.c
    lib/wasm3/source/m3_parse.c
    lib/wasm3/source/m3_bind.c
    lib/wasm3/source/m3_code.c
    lib/wasm3/source/m3_module.c
    lib/wasm3/source/m3_function.c
    lib/wasm3/source/m3_info.c
    lib/wasm3/source/m3_validate.c
)

target_include_directories(freewili_main PRIVATE
    include
    ../common
    lib/littlefs
    lib/wasm3/source
)

target_compile_definitions(freewili_main PRIVATE
    d_m3MaxFunctionStackHeight=500
    d_m3CodePageAlignSize=4096
    d_m3MaxLinearMemoryPages=4
    d_m3VerboseErrorMessages=1
    d_m3RecordBacktraces=0
    d_m3HasFloat=0
)

# Link Pico SDK libraries
target_link_libraries(freewili_main
    pico_stdlib
    pico_multicore
    hardware_uart
    hardware_spi
    hardware_i2c
    hardware_gpio
    hardware_pwm
    hardware_watchdog
    hardware_flash
    hardware_exception
    pico_flash
    pico_unique_id
)

# USB CDC for host serial (stdio over USB, not UART)
pico_enable_stdio_usb(freewili_main 1)
pico_enable_stdio_uart(freewili_main 0)

# Generate UF2 output for drag-and-drop flashing
pico_add_extra_outputs(freewili_main)

# Place critical ISR functions in SRAM (not flash)
# This is handled by __not_in_flash_func() in the source,
# but we also set the binary type to ensure the linker
# places the right sections.
