cmake_minimum_required(VERSION 3.12)

## Include pico cmake file
include(pico_sdk_import.cmake)

## Set project name
project(powman_states)

## Init pico CMake stuff
pico_sdk_init()

add_executable(powman_states
    powman_states.c
)

# pull in common dependencies
target_link_libraries(powman_states pico_stdlib hardware_powman)

# enable usb output, disable uart output
pico_enable_stdio_usb(powman_states 0)
pico_enable_stdio_uart(powman_states 1)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(powman_states)

