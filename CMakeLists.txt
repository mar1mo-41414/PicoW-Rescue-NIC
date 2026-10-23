cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# Firmware version — override from command line to match a git tag:
#   cmake .. -DFW_VERSION="v1.1"
# CI (release.yml) passes -DFW_VERSION="${GITHUB_REF_NAME}" automatically.
# ---------------------------------------------------------------------------
if(NOT DEFINED FW_VERSION)
    set(FW_VERSION "v1.0")
endif()

# PICO_BOARD must be set BEFORE include(pico_sdk_import.cmake) so that the
# toolchain file picks up the correct board (pico_w has CYW43 / WiFi).
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico_w CACHE STRING "Board type")
endif()

# ---------------------------------------------------------------------------
# WiFi credentials — override at configure time WITHOUT editing this file:
#   cmake .. -DWIFI_SSID="MyRescue" -DWIFI_PASSWORD="long-passphrase" -DWIFI_CHANNEL=11
# or just use the wrapper:  ./scripts/build.sh --ssid MyRescue --pass 'long-passphrase'
#
# CHANGE THE DEFAULT PASSWORD. "picobridge123" is a published value in this
# repository, not a secret — anyone who read the README knows it.
# ---------------------------------------------------------------------------
set(WIFI_SSID     "PicoBridge"    CACHE STRING "WiFi AP SSID")
set(WIFI_PASSWORD "picobridge123" CACHE STRING "WiFi AP WPA2 passphrase (8-63 chars) — CHANGE THIS")
set(WIFI_CHANNEL  "6"             CACHE STRING "2.4GHz WiFi channel (1-13)")

# Pull in Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(picow_nic C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---------------------------------------------------------------------------
# Git hash — short commit hash for build identification
# Falls back to "unknown" when building outside a git repository.
# ---------------------------------------------------------------------------
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

add_executable(picow_nic
    src/main.c
    src/stdio_cdc.c
    src/usb_descriptors.c
    src/usb_net.c
    src/wifi_ap.c
    src/network.c
    src/dhcpserver.c
    src/nat.c          # self-contained NAPT (ip4_napt.c not in this SDK)
    src/status_led.c   # onboard LED status/blink-code indicator
)

target_include_directories(picow_nic PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}        # for tusb_config.h, lwipopts.h
    ${CMAKE_CURRENT_LIST_DIR}/src    # for project headers
)

target_compile_definitions(picow_nic PRIVATE
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
    WIFI_CHANNEL=${WIFI_CHANNEL}
    PICO_STDIO_USB=0
    FW_VERSION="${FW_VERSION}"
    GIT_HASH="${GIT_HASH}"
)

# Warn loudly at configure time if the published default password is still set.
if(WIFI_PASSWORD STREQUAL "picobridge123")
    message(WARNING
        "WIFI_PASSWORD is still the published default 'picobridge123'. "
        "Rebuild with -DWIFI_PASSWORD=\"your-own-passphrase\" "
        "(or ./scripts/build.sh --pass ...) before real use.")
endif()

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
message(STATUS "  Version:       ${FW_VERSION}")
message(STATUS "  Git hash:      ${GIT_HASH}")
message(STATUS "  WiFi SSID:     ${WIFI_SSID}")
message(STATUS "  WiFi channel:  ${WIFI_CHANNEL}")
message(STATUS "  USB subnet:    10.0.0.0/24")
message(STATUS "  WiFi subnet:   192.168.4.0/24")
