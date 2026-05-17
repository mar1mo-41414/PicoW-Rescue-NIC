cmake_minimum_required(VERSION 3.13)

# PICO_BOARD must be set BEFORE include(pico_sdk_import.cmake) so that the
# toolchain file picks up the correct board (pico_w has CYW43 / WiFi).
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico_w CACHE STRING "Board type")
endif()

# Pull in Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(picow_nic C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(picow_nic
    src/main.c
    src/stdio_cdc.c
    src/usb_descriptors.c
    src/usb_net.c
    src/wifi_ap.c
    src/network.c
    src/dhcpserver.c
    src/nat.c          # self-contained NAPT (ip4_napt.c not in this SDK)
)

target_include_directories(picow_nic PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}        # for tusb_config.h, lwipopts.h
    ${CMAKE_CURRENT_LIST_DIR}/src    # for project headers
)

target_compile_definitions(picow_nic PRIVATE
    WIFI_SSID="PicoBridge"
    WIFI_PASSWORD="picobridge123"
    WIFI_CHANNEL=6
    PICO_STDIO_USB=0
)

# ip4_napt.c is absent from the pico-sdk lwIP snapshot (added post-2.1.x).
# We ship our own minimal NAPT in src/nat.c.
message(STATUS "NAPT: using bundled src/nat.c")

# ---------------------------------------------------------------------------
# Libraries
# pico_cyw43_arch_lwip_poll: cooperative poll mode (no RTOS), integrates
# cleanly with TinyUSB's own cooperative tud_task() loop.
# ---------------------------------------------------------------------------
target_link_libraries(picow_nic
    pico_stdlib
    pico_unique_id                               # board serial → USB descriptor
    pico_cyw43_arch_lwip_poll                    # WiFi + lwIP, poll mode
    tinyusb_device                               # TinyUSB device stack
    tinyusb_board                                # board support layer
)

# Debug output over UART (pin 0/1); USB is occupied by CDC-NCM
pico_enable_stdio_usb(picow_nic 0)
pico_enable_stdio_uart(picow_nic 1)

# Generate .uf2 / .bin / .hex
pico_add_extra_outputs(picow_nic)

message(STATUS "PicoW-NIC configured")
message(STATUS "  WiFi SSID:     PicoBridge")
message(STATUS "  USB subnet:    10.0.0.0/24")
message(STATUS "  WiFi subnet:   192.168.4.0/24")
