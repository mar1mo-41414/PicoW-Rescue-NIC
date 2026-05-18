# PicoW-Rescue-NIC v1.0 Specification

---

## Overview

Firmware for the Raspberry Pi Pico W that turns it into a USB-WiFi bridge NIC (Network Interface Card).

When plugged into a server via USB, the Pico W:
- Appears as a standard USB Ethernet adapter (CDC-NCM) on the server
- Simultaneously runs a WiFi Access Point
- Routes and NATs IP traffic between the two networks

This provides **out-of-band network access** to the server, independent of the server's primary network stack. If the server's normal network is misconfigured or locked down, the Pico W remains reachable via WiFi.

```
[Linux/Windows server] ──USB(CDC-NCM)──[Pico W]──WiFi(AP)──[Phone/Laptop]
        10.0.0.2                         10.0.0.1              192.168.4.10
                                        192.168.4.1
```

---

## Network Specification

| Parameter | Value |
|-----------|-------|
| USB subnet | `10.0.0.0/24` |
| Pico USB IP | `10.0.0.1` |
| USB client IP | `10.0.0.2` (DHCP, fixed) |
| WiFi SSID | `PicoBridge` (configurable) |
| WiFi password | `picobridge123` (configurable) |
| WiFi channel | 6 (configurable) |
| WiFi security | WPA2-AES |
| WiFi subnet | `192.168.4.0/24` |
| Pico WiFi IP | `192.168.4.1` |
| WiFi client IP | `192.168.4.10` (DHCP, fixed) |

### Automatic Route Distribution

The USB DHCP server sends DHCP option 121 (Classless Static Route, RFC 3442) with:

```
192.168.4.0/24  via  10.0.0.1
```

Linux hosts honor this option automatically. The route appears as:
```bash
ip route | grep 192.168.4
# 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp src 10.0.0.2 metric 101
```

No manual `ip route add` required. The route is managed by the DHCP client and survives reboots.

---

## Hardware Requirements

| Component | Specification |
|-----------|--------------|
| MCU | Raspberry Pi Pico W (RP2040 + CYW43439) |
| USB cable | USB-A to Micro-USB, data-capable |
| Host | Any Linux / macOS / Windows 10+ machine with USB port |
| Power | USB bus power (~100 mA typical) |

The Pico W costs approximately $6 USD. No additional components are required.

---

## Software Dependencies

| Component | Version | Source |
|-----------|---------|--------|
| Pico SDK | 2.x | [raspberrypi/pico-sdk](https://github.com/raspberrypi/pico-sdk) |
| TinyUSB | 0.15+ | Bundled with Pico SDK |
| lwIP | 2.1.x | Bundled with Pico SDK |
| Compiler | arm-none-eabi-gcc | Package manager |
| CMake | 3.13+ | Package manager |

---

## Supported Protocols

| Protocol | USB→WiFi | WiFi→USB | Notes |
|----------|----------|----------|-------|
| ICMP (ping) | ✅ | ✅ | Full NAT with ID translation |
| TCP (SSH, curl, HTTP) | ✅ | ✅ | Full NAPT with port translation |
| UDP | ✅ | ✅ | Full NAPT with port translation |
| IPv6 | ❌ | ❌ | Not implemented (`LWIP_IPV6=0`) |

---

## USB Device Configuration

The Pico W presents as a **composite USB device** with two interfaces:

| Interface | USB Class | OS Name | Purpose |
|-----------|-----------|---------|---------|
| CDC-NCM + IAD | 0x02/0x0D | `picow`, `usb0`, `enp*` | USB Ethernet (IP traffic) |
| CDC-ACM | 0x02/0x02 | `/dev/ttyACM*`, `COM*` | Debug serial console |

**MAC Address:** `02:02:84:69:60:00` (locally administered, fixed)

**CDC-NCM** was chosen over RNDIS for plug-and-play compatibility on Linux and macOS without drivers. Windows 10 version 1903+ supports CDC-NCM natively.

---

## NAPT (Network Address and Port Translation)

Custom bidirectional NAPT implemented in `src/nat.c` using `LWIP_HOOK_IP4_INPUT`.

### NAT Table

- **Size:** 64 entries (configurable via `NAT_TABLE_SIZE` in `nat.h`)
- **Port range:** 49152–65535 (IANA dynamic/private range)
- **TTL:** TCP 120s, UDP 30s, ICMP 10s

### Translation Flows

| Direction | Source Masquerade | Destination Restore |
|-----------|------------------|---------------------|
| USB → WiFi | `nat_outbound`: client IP → `192.168.4.1` | `nat_inbound`: → original client IP |
| WiFi → USB | `nat_outbound2`: client IP → `10.0.0.1` | `nat_inbound2`: → original client IP |

### Checksum Handling

`ip4_forward()` in lwIP zeros all transport checksums when `CHECKSUM_GEN_*=1` (hardware offload expected). Neither the USB CDC-NCM driver nor the CYW43 driver implements checksum offload.

Software checksum engines recompute all checksums immediately before transmission:
- **USB TX:** `usb_fill_checksums()` in `usb_net.c`
- **WiFi TX:** `wifi_fill_checksums()` via linkoutput wrapper in `network.c`

---

## DHCP Server

Two independent DHCP servers, one per interface (`src/dhcpserver.c`):

| Interface | Assigned IP | Lease Time | Option 121 |
|-----------|-------------|------------|------------|
| USB | `10.0.0.2` (fixed) | 86400s (24h) | `192.168.4.0/24 via 10.0.0.1` |
| WiFi | `192.168.4.10` (fixed) | 86400s (24h) | (none) |

Both servers respond only to the known MAC address to prevent accidental leases to other devices.

---

## Debug Console

The CDC-ACM interface provides a real-time debug console.

**Connect:**
```bash
screen /dev/ttyACM0 115200    # Linux
screen /dev/cu.usbmodem* 115200   # macOS
# Windows: COM port in Device Manager, PuTTY at 115200 8N1
```

**Boot log:**
```
================================================
 PicoW-NIC  USB-WiFi Bridge
 USB subnet : 10.0.0.0/24
 WiFi subnet: 192.168.4.0/24  SSID=PicoBridge
================================================

WiFi AP: SSID=PicoBridge  Pico=192.168.4.1  Client=192.168.4.10
Network: IP forwarding enabled (IP_FORWARD=1)
Network: NAPT active
Network: WiFi TX checksum wrapper installed

Ready — connect PC to USB and/or WiFi AP "PicoBridge"
```

**Periodic status** (every 15 seconds):
```
--- Status ---
USB  (us1): IP=10.0.0.1         link=UP
WiFi (w10): IP=192.168.4.1      link=UP  RSSI=-45 dBm
```

**Verbose mode** (`VERBOSE_LOG=1` in `lwipopts.h`, rebuild required):
```
USB RX: 98 bytes  IPv4 proto=1 dport=12345
NAT ICMP: inp=w1 src=192.168.4.10 dst=10.0.0.2 type=8
USB TX: 98 bytes  ready=1 can_xmit=1
```

---

## Configuration

All user-configurable settings are in `CMakeLists.txt`:

```cmake
target_compile_definitions(picow_nic PRIVATE
    WIFI_SSID="PicoBridge"        # WiFi AP name
    WIFI_PASSWORD="picobridge123"  # WPA2 passphrase
    WIFI_CHANNEL=6                 # 2.4 GHz channel (1–13)
    PICO_STDIO_USB=0               # USB stdio disabled (CDC-ACM handled by stdio_cdc.c)
)
```

IP addresses and subnet sizes are defined in source:

| Symbol | File | Default |
|--------|------|---------|
| Pico USB IP | `usb_net.c` | `10.0.0.1` |
| USB client IP | `usb_net.c` | `10.0.0.2` |
| Pico WiFi IP | `wifi_ap.c` | `192.168.4.1` |
| WiFi client IP | `dhcpserver.c` | `192.168.4.10` |
| NAT table size | `nat.h` | 64 |

---

## Build

```bash
# Prerequisites (Ubuntu/Debian)
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Clone and build
git clone https://github.com/YOUR_USERNAME/PicoW-Rescue-NIC
cd PicoW-Rescue-NIC
mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# Output: build/picow_nic.uf2
```

---

## Flash

```bash
# Hold BOOTSEL, plug USB, release button
# RPI-RP2 drive appears
cp build/picow_nic.uf2 /media/$USER/RPI-RP2/
# Pico reboots automatically and starts running firmware
```

---

## Known Limitations

| Limitation | Detail |
|-----------|--------|
| One WiFi client | Fixed IP DHCP — second device needs static IP configuration |
| USB Full Speed | 12 Mbps physical; ~2–4 Mbps effective through NAT |
| IPv6 only | `LWIP_IPV6=0` — IPv4 only |
| WiFi client route | WiFi→USB direction requires manual `ip route add` on WiFi side |
| No additional auth | WPA2 on WiFi, but the bridge itself has no extra authentication layer |
| Pico W only | Tested on RP2040 + CYW43439 only |

---

## Tested Environments

| Role | OS / Device | Interface | Status |
|------|-------------|-----------|--------|
| USB host | Ubuntu 22.04 LTS | CDC-NCM (`picow`) | ✅ Verified |
| USB host | Ubuntu 24.04 LTS | CDC-NCM | ✅ Verified |
| USB host | Windows 10/11 | CDC-NCM | ✅ Verified |
| WiFi client | macOS 14 Sonoma | WiFi (WPA2) | ✅ Verified |
| WiFi client | Android (various) | WiFi (WPA2) | ✅ Verified |

---

## Version History

| Version | Date | Notes |
|---------|------|-------|
| v1.0 | 2026-05-18 | First stable release. All bidirectional TCP/UDP/ICMP working. DHCP option 121. |
