# 🆘 PicoW-Rescue-NIC

> **Your server's network emergency exit — a plug-in escape hatch, independent of the OS network stack.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Pico%20W-red)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![SDK](https://img.shields.io/badge/Pico%20SDK-2.x-blue)](https://github.com/raspberrypi/pico-sdk)
[![CI Build](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/actions/workflows/build.yml/badge.svg)](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/mar1mo-41414/PicoW-Rescue-NIC)](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/releases/latest)

[日本語版 → README.md](README.md)

---

## 🧭 Read this first: what this tool is for

This is a **one-off, personal, hobby-built emergency recovery tool.** It is **not** a general-purpose USB-WiFi bridge product.

It targets exactly one situation:

> **You locked yourself out of a server with a bad network-config change. You have no physical console on hand. But the machine itself is still alive.**

You plug a Pico W in and, **without touching the server at all**, grow "one more path in" (an extra NIC) that restores *the road to a door you can log in through*. That is the whole job. If you get just enough reachability back to SSH in, fix the config, and return to your normal management setup — done.

**The most important point:**

- This tool restores **network-layer reachability only.**
- It works **only if a usable login entry point is still alive inside the server.** That entry point does not have to be SSH. If SSH is dead but some *other* console you can log in through is still up (a serial login, another live service, etc.), recovery is still possible.
- Conversely, if **no entry point survives at all** (sshd and every other login path gone, the server powered off, the disk broken so you can't log in) — this tool can do nothing.
- In other words: it fixes the state where **"the door exists, but only the road to it (the network) is broken."** It does not *create* a door. It gives you a **road to the door you already have.**

It is **not** meant to be left plugged in permanently. Keep it in a drawer; **plug it in during an incident and unplug it when you're done.** Keep one *on hand*, not *installed*.

---

## The Problem

You've been there.

```
[You, at home] ──── Internet ──✗── [Your server, somewhere far away]
```

You forgot to undo `ip link set eth0 down`.
You gave two interfaces the same static IP.
You edited `ifupdown` / `NetworkManager` config and the box came back with no IP after a reboot.
You killed NetworkManager while testing and forgot.
You fat-fingered a routing table entry.

**The machine is alive. The disk is fine. The OS is running. sshd is listening. But you cannot reach it over the network.**

You need physical access — or you need to call someone.

---

## The Solution

Plug a **~$6 Raspberry Pi Pico W** into any spare USB port on the server.

```
[You, at home]
      │
      ▼ WiFi (192.168.4.0/24)
 ┌────────────┐
 │  Pico W    │  ← 4cm × 2cm, USB-powered
 │  Rescue NIC│
 └────────────┘
      │ USB CDC-NCM (10.0.0.0/24)
      ▼
[Locked-out server]
  - to the server it's just "one more NIC" (10.0.0.2)
  - SSH / curl / ping work through this NIC
  - → log in to a door that's still alive, fix the config
```

The Pico W appears as a **standard USB Ethernet adapter** (CDC-NCM) on the server.
It simultaneously runs a **WiFi Access Point**.
You connect from your phone or laptop over WiFi, SSH through the bridge, and fix what's broken.

**No drivers. No cloud. No vendor lock-in. Works offline.**

> ⚠️ Note: adding a NIC does **not** rescue an **all-interfaces** lockout such as
> `iptables -P INPUT DROP`, nor a stopped/broken **sshd**. See the table below for
> exactly what is and isn't recoverable.

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

## Failure modes: what it can and cannot rescue

This tool rescues one class of failure: **the network layer is broken, but the OS and a login entry point are still alive.** Everything else is out of scope.

### ✅ Can rescue (NIC / route / link-layer failures)

| Failure | Why |
|---------|-----|
| Wrong IP config on a NIC, or interface is down | A directly-attached second NIC (10.0.0.0/24) restores L3 reachability |
| Default route deleted, or a bad route added | A directly-attached subnet needs no route |
| NetworkManager / systemd-networkd / ifupdown stopped or misconfigured | Works off the kernel's USB NCM + the Pico's DHCP alone |
| Duplicate static IP causing a subnet clash | 10.0.0.0/24 is independent, so no clash |
| No IP after a reboot | The Pico hands out IP and routes over DHCP |
| WiFi adapter or driver failure | The WiFi lives on the Pico |
| Firewall rules scoped to **a specific interface** (`-i eth0`) | The freshly-added NIC is outside those rules |

### ❌ Cannot rescue (the door itself / power / storage)

| Failure | Why | Alternative |
|---------|-----|-------------|
| `-P INPUT DROP` / DROP rules not scoped to an interface | netfilter's verdict doesn't change just because you added a NIC | Physical console / PiKVM |
| sshd stopped/broken **and no other console to log in through** | No surviving entry point to connect to | Physical console / serial / PiKVM |
| Disk full / fsck prompt / kernel panic | You can't log in at all | PiKVM / serial console |
| The server is powered off or won't boot | The Pico is USB bus-powered; no power → no Pico | On-site handling |

> **An `iptables` lockout is not always recoverable.**
> If the rules are interface-scoped (`-i <if>`), a new NIC can get through.
> If the default policy is `DROP` and applies to **all** interfaces, adding a NIC
> changes nothing. Check your own rule layout before you rely on this.

---

## What this is not (non-goals)

- **Not a general-purpose USB-WiFi bridge.** Its only purpose is recovering a server that has locked you out of the network.
- **Not a permanently installed device.** Keep it in a drawer; plug in during an incident, unplug when done.
- **Not for connecting multiple clients.** One admin machine is the intended use. Multiple clients, LAN extension, acting as your uplink — out of scope.
- **Not a feature-rich router.** No Web UI, DNS forwarder, VPN, port forwarding, or packet capture. Use real network gear for those.
- **Not a performance tool.** ~2–4 Mbps in practice. Not for bulk SCP.
- **Not a commercial product.** A personal hobby project. No support, warranty, or SLA.

---

## ⚠️ Before you use it: change the default password

The **SSID `PicoBridge` / password `picobridge123` written in this repo are published values, not credentials.** Everyone who read the README knows them. **Always build with your own values before real use.**

```bash
# One command, no file editing — pass your settings as arguments:
./scripts/build.sh --ssid "MyRescueNet" --pass "a-long-passphrase-of-your-own"
```

(See [Build](#build) for details.)

### This is an escape hatch, and misuse turns it into a backdoor

A Pico W flashed with this firmware **creates a network path into the machine the moment you plug in USB.** That is the design. The flip side: left plugged in, it becomes a permanently open back door. So:

1. **Change the default SSID / password before use.** (above)
2. **Unplug it when you're done.** Don't leave it installed. That unplug *is* the arming mechanism.
3. **Don't leave it plugged in in untrusted locations.** Anyone in WiFi range can try to associate.
4. **Verify the server afterwards** — check for leftover interfaces/settings and unexpected logins.

**Threat model:** this is a tool for someone who **already has physical access** to the server to save a trip on-site. It is not a tool for someone *without* physical access to get in remotely (it assumes you can plug in USB; it does not break physical security).

**Logging:** connection history is **not recorded by default.** If you want to know what happened during a recovery, capture the CDC-ACM serial log on the host while you work (`sudo cat /dev/ttyACM0 | tee rescue-$(date +%F).log`).

---

## Features

| Feature | Detail |
|---------|--------|
| 🔌 **USB Ethernet (CDC-NCM)** | Plug-and-play on Linux (other OSes: see [support table](#verified-environments-support-table)) |
| 📡 **WiFi Access Point** | WPA2-AES, configurable SSID / password / channel |
| 🔄 **Bidirectional IP Routing** | Bundled NAPT — SSH, curl, ping |
| 🏷️ **Auto Route Distribution** | DHCP option 121 pushes the WiFi route to the server automatically |
| 💡 **LED status indicator** | Single LED, blink patterns for state/error codes ([LED status](#led-status)) |
| 🖥️ **Debug Console** | CDC-ACM serial — firmware logs over the same USB cable |
| ⚡ **Zero Dependencies** | No OS, no RTOS, no kernel modules required |
| 🔋 **USB Bus-powered** | Draws ~100mA — survives server reboots via USB |

---

## Use Cases

All of these are variants of the same job: **emergency recovery from a network-config lockout.**

### 🏠 Homelab & Self-hosted

- Locked out after misconfiguring `iptables` / `nftables` / routing (within the [recoverable set](#failure-modes-what-it-can-and-cannot-rescue))
- Keep one **on hand as a recovery option** before testing network changes
- Temporary remote access to a machine with no WiFi card

### 🏢 Datacenter / Colocation

- Emergency access before an on-site trip when the management network is down
- A cheap secondary OOB (Out-of-Band) path where IPMI/iDRAC is absent or unusable

> Note: this is **not** a "permanently installed break-glass" device. The intended flow is plug in during an incident, unplug once recovered.

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
- USB-A to Micro-USB cable (**must carry data** — a charge-only cable won't work)
- Host: any Linux/macOS/Windows machine with a spare USB port (see [support table](#verified-environments-support-table))

> **Why not Pico 2 W?** Pico 2 W (RP2350) should work with minor SDK changes but hasn't been tested. PRs welcome.

---

## Verified environments (support table)

**The author only has the hardware the author owns.** Anything not marked "✅ Verified" below simply means the author hasn't confirmed it on real hardware — not that it fails. Reports and PRs about what worked / didn't work on your gear are very welcome.

### USB host side (the server — the side that sees the CDC-NCM NIC)

| Environment | Status | Notes |
|-------------|--------|-------|
| Linux Ubuntu 22.04 / 24.04 (kernel 5.15+) | ✅ Verified | `cdc_ncm` loads by default; confirmed working |
| Other Linux distros (kernel with `cdc_ncm`) | ⚪ Untested (very likely works) | Check with `sudo modprobe cdc_ncm` |
| macOS | ⚪ Untested | CDC-NCM should be supported out of the box |
| Windows 10 (1903+) / 11 | ⚪ Untested | CDC-NCM supported, but USB re-enumeration / power-saving flakiness is commonly reported |
| Windows 7 / 8 / embedded Windows | ❌ Not supported | No CDC-NCM; needs RNDIS (not implemented, on roadmap) |
| ESXi / Windows Server / FreeBSD / OpenWrt / Proxmox / TrueNAS, etc. | ❓ Untested | Author has no hardware to check; depends on CDC-NCM driver availability. Reports / PRs welcome |

### WiFi client side (your admin PC / phone — the side that joins the AP)

| Environment | Status | Notes |
|-------------|--------|-------|
| iPhone (iOS) | ✅ Verified | |
| macOS 14 (Sonoma) | ✅ Verified | |
| Android | ⚪ Untested (likely works) | Plain WPA2-PSK; needs the [WiFi-side route](#adding-a-route-on-the-wifi-side-wifi--usb-direction) |
| Windows / Linux laptop | ⚪ Untested (likely works) | Add a route manually after connecting |

---

## Build

### Download a pre-built firmware (easiest)

No toolchain needed — grab the latest `.uf2` directly from [**GitHub Releases**](https://github.com/mar1mo-41414/PicoW-Rescue-NIC/releases/latest).

```
1. Open the Releases page
2. Under the latest release "Assets", download picow_nic_vX.X.uf2
3. Hold BOOTSEL on the Pico W while plugging USB
4. Copy the file to the RPI-RP2 drive that appears — done
```

> ⚠️ **The released build ships with the default SSID/password (`PicoBridge` / `picobridge123`).**
> Using those published values as-is is unsafe — rebuild with your own values before real use (below).

### Prerequisites

```bash
# Install ARM toolchain (Ubuntu/Debian)
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential

# Clone Pico SDK
git clone https://github.com/raspberrypi/pico-sdk --recurse-submodules
export PICO_SDK_PATH=/path/to/pico-sdk
```

### One-command build (recommended) — change settings without editing files

`scripts/build.sh` takes the SSID, password and channel as arguments. **You do not need to edit `CMakeLists.txt`.**

```bash
git clone https://github.com/mar1mo-41414/PicoW-Rescue-NIC
cd PicoW-Rescue-NIC

# Build with your own SSID / password (always change these!)
./scripts/build.sh --ssid "MyRescueNet" --pass "a-long-passphrase-of-your-own"

# With a channel too
./scripts/build.sh --ssid "MyRescueNet" --pass "..." --channel 11

# Wipe the build dir first
./scripts/build.sh --clean --ssid "MyRescueNet" --pass "..."

# All options
./scripts/build.sh --help
```

Output: `build/picow_nic.uf2`

> WPA2 passphrases are 8–63 characters; the script checks the length.
> If you omit `--pass`, the published default is used and cmake prints a warning.

### Manual build (classic)

```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release \
  -DWIFI_SSID="MyRescueNet" -DWIFI_PASSWORD="a-long-passphrase-of-your-own" -DWIFI_CHANNEL=6
make -j$(nproc)
```

Omitting the `-DWIFI_*` flags uses the defaults (`PicoBridge` / `picobridge123` / ch 6).

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

Join the **SSID you set at build time** (default: `PicoBridge` / password `picobridge123`) from your phone or laptop.

```
Your device gets: 192.168.4.10
```

### Step 3: Reach the Server

```bash
# From your phone/laptop (on the WiFi AP)
ssh user@10.0.0.2        # SSH into the locked-out server
curl http://10.0.0.2      # HTTP
ping 10.0.0.2             # Ping
```

### Adding a route on the WiFi side (WiFi → USB direction)

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

## LED status

The Pico W has **only one single-colour LED**, so it conveys state via **blink patterns** — Raspberry Pi 4 PWR-LED style, encoding a code with long and short flashes.

| LED behaviour | Meaning |
|---------------|---------|
| Near-solid (winks off briefly once every ~2 s) | ✅ Fully operational. USB host up + a WiFi client connected. Ready to work |
| Even 1 Hz blink | Up, waiting for a WiFi client. Join the AP → it goes to fully operational |
| **1 long + 2 short** (repeating = code 2) | USB host not seen. The server isn't enumerating the NIC (OS-side USB / cable / port) |
| **1 long + 3 short** (repeating = code 3) | WiFi AP is not up. Likely a firmware fault |
| Completely dark (powered, but no light for >1 s) | WiFi chip init failed (fatal). The LED lives on the WiFi chip, so it can't even blink a code |
| Completely dark (no light at all) | No power → server off / USB not powering / bad cable. Or the first ~1 s of boot |

> How to read the codes and isolate each state: [`docs/troubleshooting.md`](docs/troubleshooting-EN.md#led-status-codes).

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
 Pico Rescue Bridge v1.0
 RP2040 USB Rescue NIC
================================================
 USB subnet : 10.0.0.0/24
 WiFi subnet: 192.168.4.0/24  SSID=PicoBridge
================================================

WiFi AP: SSID=PicoBridge  Pico=192.168.4.1  Client=192.168.4.10
Network: IP forwarding enabled (IP_FORWARD=1)
Network: NAPT active
Network: WiFi TX checksum wrapper installed

Ready — connect PC to USB and/or WiFi AP "PicoBridge"
LED: near-solid=OK  1Hz blink=waiting for WiFi client
LED: long+2short=USB host not seen  long+3short=WiFi AP down

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

See [`docs/architecture.md`](docs/architecture-EN.md) for full design notes.
See [`docs/investigation.md`](docs/investigation-EN.md) for the debugging journey and bug analysis.

---

## Project Structure

```
PicoW-Rescue-NIC/
├── README.md               Japanese
├── README-EN.md            this file (English)
├── LICENSE
├── CMakeLists.txt          Build config (WiFi settings are cache vars = override with -D)
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
│   ├── status_led.c/h      Single-LED status / blink-code indicator
│   ├── usb_descriptors.c/h Composite USB descriptor (IAD)
│   └── stdio_cdc.c         printf → CDC-ACM serial
│
├── docs/
│   ├── architecture.md     Design decisions, data flow diagrams
│   ├── investigation.md    Bug analysis: checksums, NAT, lwIP internals
│   ├── troubleshooting.md  Common problems + the LED code table
│   ├── spec.md             Full specification
│   └── *-EN.md             English versions of each doc
│
├── images/                 Photos, diagrams
└── scripts/
    ├── build.sh            One-command build; pass settings as arguments
    └── flash.sh            One-command flash helper
```

---

## Known Limitations

| Limitation | Notes |
|-----------|-------|
| One WiFi client | DHCP assigns a fixed IP; one admin machine is the assumption |
| USB Full Speed | 12 Mbps physical limit; effective ~2–4 Mbps through NAT |
| IPv6 not supported | `LWIP_IPV6=0` — IPv4 only. IPv6-only environments are stuck (planned) |
| WiFi client needs a manual route | For WiFi→USB direction; USB→WiFi is automatic via DHCP opt 121 |
| Settings are build-time | SSID/pass/channel are fixed at build (but `build.sh` args set them without editing files) |
| No extra auth | WiFi is WPA2, but the bridge itself has no extra auth layer. Mitigate with short usage windows + a long PSK |
| No audit log | Connection history isn't kept by default (see [Logging](#this-is-an-escape-hatch-and-misuse-turns-it-into-a-backdoor)) |
| Pico W only | Tested on RP2040 + CYW43439 only |

See [Failure modes](#failure-modes-what-it-can-and-cannot-rescue) for what is / isn't recoverable.

---

## Roadmap / TODO

- [ ] **IPv6** — SLAAC first (TBD)
- [ ] **Multiple WiFi clients** — DHCP lease table instead of fixed IP
- [ ] **RNDIS support** — for older Windows versions
- [ ] **Pico 2 W (RP2350)** — port and test
- [ ] **Expand the support table** — ESXi / Windows Server / BSD, etc. (verification & PRs welcome if you have the hardware)
- [x] **Change settings without a build edit** — handled via `build.sh` arguments (SSID/pass/channel)
- [x] **LED status indicator** — blink-code state display

---

## Contributing

Issues and PRs are welcome. Before submitting:

1. Read [`docs/architecture.md`](docs/architecture-EN.md) for design context
2. Check [`docs/investigation.md`](docs/investigation-EN.md) for known NAT/checksum gotchas
3. Run a basic ping + SSH test in both directions before submitting
4. Reports of what worked / didn't on your OS/hardware help expand the [support table](#verified-environments-support-table)

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

> *Photo: Pico W, and the server with `ip route` before the Pico W was connected*
[`images/1.png`](images/1.png) 

> *Photo: `ip route` on the server after connecting the Pico W (a route has been added)*
[`images/2.png`](images/2.png) 

> *Photo: Wi-Fi settings on an iPhone connected to the Pico W AP*
[`images/3.png`](images/3.png) 

> *Photo: SSH session connected through the Pico W (showing neofetch and a ping from the PC to the iPhone)*
[`images/4.png`](images/4.png) 

> *Photo: Pinging the iPhone through the Pico W*
[`images/5.png`](images/5.png) 

</details>
