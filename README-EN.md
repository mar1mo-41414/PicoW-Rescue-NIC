# 🆘 PicoW-Rescue-NIC

> **Your server's emergency exit — always on, independent of the OS network stack.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Pico%20W-red)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![SDK](https://img.shields.io/badge/Pico%20SDK-2.x-blue)](https://github.com/raspberrypi/pico-sdk)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#build)

---

## The Problem

You've been there.

```
[You, at home] ──── Internet ──✗── [Your server, somewhere far away]
```

You ran `iptables -F` and forgot to re-open SSH.  
You misconfigured a network interface.  
You killed NetworkManager while testing something.  
You fat-fingered a routing table entry.

**The machine is alive. The disk is fine. But you cannot get in.**

You need physical access — or you need to call someone.

---

## The Solution

Plug a **$6 Raspberry Pi Pico W** into any spare USB port on the server.

```
[You, at home]
      │
      ▼ WiFi (192.168.4.0/24)
 ┌────────────┐
 │  Pico W    │  ← 4cm × 2cm, always-on, USB-powered
 │  Rescue NIC│
 └────────────┘
      │ USB CDC-NCM (10.0.0.0/24)
      ▼
[Locked-out server]
  - SSH still works
  - curl still works
  - ping still works
  - iptables/routing
    changes still apply
```

The Pico W appears as a **standard USB Ethernet adapter** (CDC-NCM) on the server.  
It simultaneously runs a **WiFi Access Point**.  
You connect from your phone or laptop over WiFi, SSH through the bridge, and fix what's broken.

**No drivers. No cloud. No vendor lock-in. Works offline.**

---

> [!WARNING]
> **Use at your own risk!**
>
> There is no guarantee this will rescue your server.  
> It may not work correctly in every situation.  
> In the worst case, using this could make things worse.  
> Whatever happens, I cannot take responsibility.  
> **Test it on a non-critical machine before you actually need it.**

---

## Features

| Feature | Detail |
|---------|--------|
| 🔌 **USB Ethernet (CDC-NCM)** | Plug-and-play on Linux, macOS, Windows 10+ |
| 📡 **WiFi Access Point** | WPA2-AES, configurable SSID/password |
| 🔄 **Bidirectional IP Routing** | Full NAT/NAPT — SSH, curl, ping, SCP |
| 🏷️ **Auto Route Distribution** | DHCP option 121 pushes WiFi routes to server automatically |
| 🖥️ **Debug Console** | CDC-ACM serial — firmware logs over the same USB cable |
| ⚡ **Zero Dependencies** | No OS, no RTOS, no kernel modules required |
| 🔋 **USB Bus-powered** | Draws ~100mA — survives server reboots via USB |

---

## Use Cases

### 🏠 Homelab & Self-hosted

- Locked out of a server after misconfiguring `iptables` / `nftables`
- Testing network changes without fear — always have a fallback
- Remote access to a machine with no WiFi card

### 🏢 Datacenter / Colocation

- Emergency access when the management network is down
- Secondary OOB (Out-of-Band) path alongside IPMI/iDRAC
- Pre-installed on servers as a "break glass" option

### 🧪 Development & Lab

- Network-independent access to embedded targets
- Testing network stack changes on a live system
- Isolated test network without touching the main LAN

### 🚗 Portable / Field

- Remote access point for a laptop in the field
- USB tether with NAT to share WiFi over USB
- Lightweight network bridge for demos

---

## Network Topology

```
                    ┌─────────────────────────────┐
                    │       Raspberry Pi Pico W    │
                    │                             │
[WiFi Clients]      │  192.168.4.1  ←→  10.0.0.1  │      [USB Server]
192.168.4.10 ───────│  WiFi AP               CDC-NCM│─────  10.0.0.2
                    │      ↕ NAPT/Routing ↕         │
                    └─────────────────────────────┘

WiFi subnet : 192.168.4.0/24   (Pico = 192.168.4.1)
USB subnet  : 10.0.0.0/24      (Pico = 10.0.0.1)
WiFi client : 192.168.4.10     (DHCP fixed)
USB server  : 10.0.0.2         (DHCP fixed)
```

### Packet Flow Example: SSH from Phone → Locked-out Server

```
Phone (192.168.4.10)
  │  SSH TCP SYN → 10.0.0.2:22
  ▼
Pico W WiFi (192.168.4.1) — receives packet
  │  NAT: src 192.168.4.10 → 10.0.0.1
  ▼
Pico W USB (10.0.0.1) — forwards via CDC-NCM
  │
  ▼
Server (10.0.0.2) — receives SSH from 10.0.0.1
  │  SSH SYN-ACK → 10.0.0.1
  ▼
Pico W USB — receives, NAT lookup
  │  dst 10.0.0.1 → 192.168.4.10 (restored)
  ▼
Phone — SSH session established ✓
```

---

## Hardware Requirements

- **Raspberry Pi Pico W** (RP2040 + CYW43439)  
  *~$6 USD — the only hardware needed*
- USB-A to Micro-USB cable
- Host: any Linux/macOS/Windows machine with a spare USB port

> **Why not Pico 2 W?** Pico 2 W (RP2350) should work with minor SDK changes but hasn't been tested. PRs welcome.

---

## Build

### Prerequisites

```bash
# Install ARM toolchain (Ubuntu/Debian)
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Clone Pico SDK
git clone https://github.com/raspberrypi/pico-sdk --recurse-submodules
export PICO_SDK_PATH=/path/to/pico-sdk
```

### Clone & Build

```bash
git clone https://github.com/YOUR_USERNAME/PicoW-Rescue-NIC
cd PicoW-Rescue-NIC

mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Output: `build/picow_nic.uf2`

### Customize WiFi Credentials

Edit `CMakeLists.txt` before building:

```cmake
target_compile_definitions(picow_nic PRIVATE
    WIFI_SSID="YourRescueNet"
    WIFI_PASSWORD="YourSecret"
    WIFI_CHANNEL=6
)
```

---

## Flash

### Method 1: UF2 (Recommended)

```bash
# Hold BOOTSEL button while plugging USB
# Drive "RPI-RP2" appears — copy the firmware
cp build/picow_nic.uf2 /media/$USER/RPI-RP2/

# Or use the helper script
./scripts/flash.sh
```

### Method 2: SWD (OpenOCD)

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "program build/picow_nic.elf verify reset exit"
```

---

## Connect

### Step 1: Plug in the Pico W

USB in → server auto-configures the CDC-NCM interface via DHCP.

```bash
# Verify on the server
ip addr show        # → 10.0.0.2/24 on picow/usb0/enp...
ip route            # → 192.168.4.0/24 via 10.0.0.1  ← auto-pushed by DHCP opt 121
```

No manual `ip route add` needed — the Pico tells the server about the WiFi subnet automatically.

### Step 2: Connect to WiFi

Join **`PicoBridge`** (password: `picobridge123`) from your phone or laptop.

```
Your device gets: 192.168.4.10
```

### Step 3: Reach the Server

```bash
# From your phone/laptop (on PicoBridge WiFi)
ssh user@10.0.0.2        # SSH into the locked-out server
curl http://10.0.0.2      # HTTP
ping 10.0.0.2             # Ping
scp file user@10.0.0.2:~ # File copy
```

### Adding a Route on WiFi Side (for WiFi → USB direction)

If your WiFi client needs to reach `10.0.0.x`:

```bash
# macOS
sudo route add -net 10.0.0.0/24 192.168.4.1

# Linux
sudo ip route add 10.0.0.0/24 via 192.168.4.1

# Windows
route add 10.0.0.0 mask 255.255.255.0 192.168.4.1
```

---

## Debug Console

The Pico exposes a second USB serial port (CDC-ACM) for firmware logs.

```bash
# Linux
screen /dev/ttyACM0 115200

# macOS
screen /dev/cu.usbmodem* 115200

# Windows
# COM port in Device Manager → PuTTY
```

### Boot Log

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

--- Status ---
USB  (us1): IP=10.0.0.1         link=UP
WiFi (w10): IP=192.168.4.1      link=UP  RSSI=-42 dBm
```

### Enable Verbose Logging

For per-packet tracing (useful for debugging NAT/routing issues):

```c
// lwipopts.h — set to 1 and rebuild
#define VERBOSE_LOG  1
```

Produces per-packet output:
```
USB RX: 98 bytes  IPv4 proto=1 dport=12345
NAT ICMP: inp=w1 src=192.168.4.10 dst=10.0.0.2 type=8
USB TX: 98 bytes  ready=1 can_xmit=1
```

---

## Technical Architecture

### Stack Overview

```
┌─────────────────────────────────────────────────────┐
│                    Application                       │
│         main.c — cooperative poll loop               │
├──────────────────┬──────────────────────────────────┤
│   TinyUSB        │         lwIP (NO_SYS=1)           │
│   CDC-NCM        │  IP_FORWARD + LWIP_HOOK_IP4_INPUT │
│   CDC-ACM        │         NAPT (nat.c)              │
├──────────────────┼──────────────────────────────────┤
│  USB peripheral  │      CYW43439 WiFi driver         │
│  (RP2040 USB)    │      (cyw43_arch_lwip_poll)       │
└──────────────────┴──────────────────────────────────┘
           RP2040 @ 125 MHz — 264 KB SRAM
```

### Key Design Decisions

**Why custom NAPT?**  
The Pico SDK's bundled lwIP snapshot does not include `ip4_napt.c` (added post-2.1.x).  
We implement a minimal NAT table in `src/nat.c` using `LWIP_HOOK_IP4_INPUT`.

**Why software checksums?**  
`ip4_forward()` zeroes IP/TCP/UDP/ICMP checksums assuming the NIC hardware will fill them in  
(checksum offload). Neither TinyUSB's CDC-NCM nor the CYW43 driver implement offload.  
We recompute all checksums in software in the `linkoutput` path for both interfaces.

**Why no RTOS?**  
TinyUSB and lwIP both have cooperative poll modes that work together cleanly.  
Adding an RTOS would complicate the `tud_task()` / `cyw43_arch_poll()` interaction  
with no benefit for this use case.

See [`docs/architecture.md`](docs/architecture.md) for full design notes.  
See [`docs/investigation.md`](docs/investigation.md) for the debugging journey and bug analysis.

---

## Project Structure

```
PicoW-Rescue-NIC/
├── README.md
├── LICENSE
├── CMakeLists.txt          Build configuration
├── lwipopts.h              lwIP tuning (VERBOSE_LOG, checksums, NAT hook)
├── tusb_config.h           TinyUSB config (CDC-NCM + CDC-ACM composite)
├── pico_sdk_import.cmake
│
├── src/
│   ├── main.c              Entry point, cooperative poll loop
│   ├── usb_net.c/h         CDC-NCM ↔ lwIP bridge + SW checksum engine
│   ├── wifi_ap.c/h         CYW43 AP mode + DHCP server init
│   ├── network.c/h         WiFi TX checksum wrapper, network_init()
│   ├── nat.c/h             Bidirectional NAPT (TCP/UDP/ICMP)
│   ├── dhcpserver.c/h      Fixed-IP DHCP server with DHCP opt 121
│   ├── usb_descriptors.c/h Composite USB descriptor (IAD)
│   └── stdio_cdc.c         printf → CDC-ACM serial
│
├── docs/
│   ├── architecture.md     Design decisions, data flow diagrams
│   ├── investigation.md    Bug analysis: checksums, NAT, lwIP internals
│   ├── troubleshooting.md  Common problems and solutions
│   └── spec.md             Full specification
│
├── images/                 Photos, diagrams (see docs)
└── scripts/
    └── flash.sh            One-command flash helper
```

---

## Known Limitations

| Limitation | Notes |
|-----------|-------|
| One WiFi client | DHCP assigns a fixed IP — multiple clients need dhcpserver expansion |
| USB Full Speed | 12 Mbps physical limit; effective ~2–4 Mbps through NAT |
| IPv6 not supported | `LWIP_IPV6=0` — IPv4 only |
| WiFi client needs manual route | For WiFi→USB direction; USB→WiFi is automatic via DHCP opt 121 |
| No encryption | WiFi uses WPA2 but the bridge itself has no additional auth |
| Pico W only | Tested on RP2040 + CYW43439 only |

---

## Roadmap / TODO

- [ ] **Multiple WiFi clients** — DHCP lease table instead of fixed IP
- [ ] **Station mode** — connect Pico to upstream WiFi, bridge to USB (reverse direction)
- [ ] **Web status page** — lwIP httpd showing link state, NAT table, RSSI
- [ ] **DNS proxy** — forward DNS queries from USB side through WiFi
- [ ] **WireGuard** — encrypted tunnel on top of the WiFi link
- [ ] **RNDIS support** — for older Windows versions
- [ ] **Pico 2 W (RP2350)** — port and test
- [ ] **USB High Speed** — external PHY or RP2350 for >12 Mbps

---

## Contributing

Issues and PRs are welcome. Before submitting:

1. Read [`docs/architecture.md`](docs/architecture.md) for design context
2. Check [`docs/investigation.md`](docs/investigation.md) for known NAT/checksum gotchas
3. Run a basic ping + SSH test in both directions before submitting

---

## Public Name Candidates

If you're forking/renaming this project, here are some alternatives:

| Name | Vibe |
|------|------|
| **pico-rescue-nic** | Descriptive, search-friendly |
| **break-glass-nic** | "Break glass in emergency" sysadmin reference |
| **usb-lifeline** | Emotional, memorable |
| **pico-lastroute** | Pun on "last resort" + routing |
| **netcork** | Compact — like a cork/stopper for a network hole |
| **pico-oob** | Out-of-Band management reference |
| **picogate** | Minimal, clean |

---

## License

MIT — see [LICENSE](LICENSE)

---

## Acknowledgements

- [Raspberry Pi Foundation](https://www.raspberrypi.com/) — Pico W hardware + SDK
- [TinyUSB](https://github.com/hathach/tinyusb) — USB device stack
- [lwIP](https://savannah.nongnu.org/projects/lwip/) — TCP/IP stack
- [Pico SDK](https://github.com/raspberrypi/pico-sdk) — board support + CYW43 driver

---

<details>
<summary>📷 Photos / Demo (click to expand)</summary>

<!-- Add photos here -->
> *Photo: Pico W plugged into a server's USB port, WiFi connected from phone*

<!-- Add GIF here -->
> *GIF: SSH session established through Pico W bridge*

</details>
