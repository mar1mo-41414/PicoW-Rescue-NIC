cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(picow_nic C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(picow_nic
    src/main.c
    src/usb_descriptors.c
    src/usb_net.c
    src/wifi_ap.c
    src/network.c
    src/dhcpserver.c
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

# ---------------------------------------------------------------------------
# lwIP NAPT (NAT Port Address Translation)
# ip4_napt.c is NOT compiled by default in the bundled lwIP — add it manually.
# ---------------------------------------------------------------------------
if(DEFINED ENV{PICO_SDK_PATH})
    set(_LWIP_NAPT "$ENV{PICO_SDK_PATH}/lib/lwip/src/core/ipv4/ip4_napt.c")
    if(EXISTS "${_LWIP_NAPT}")
        target_sources(picow_nic PRIVATE "${_LWIP_NAPT}")
        message(STATUS "NAPT: found ${_LWIP_NAPT}")
    else()
        message(WARNING "ip4_napt.c not found — NAT disabled. Install pico-sdk >= 1.5")
        target_compile_definitions(picow_nic PRIVATE NO_IP_NAPT=1)
    endif()
else()
    message(WARNING "PICO_SDK_PATH not set — skipping NAPT check")
    target_compile_definitions(picow_nic PRIVATE NO_IP_NAPT=1)
endif()

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
