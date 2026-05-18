# Architecture — PicoW-Rescue-NIC

This document covers the design decisions, data flow, and internal structure of the firmware.

---

## System Overview

```
┌───────────────────────────────────────────────────────────────┐
│                     Raspberry Pi Pico W                        │
│                                                               │
│  ┌────────────────────┐        ┌──────────────────────────┐  │
│  │      TinyUSB        │        │    lwIP  (NO_SYS=1)      │  │
│  │  ┌──────────────┐  │        │  ┌────────┐ ┌─────────┐  │  │
│  │  │  CDC-NCM     │◄─┼────────┼─►│usb_netif│ │wifi_netif│  │  │
│  │  │  (Ethernet)  │  │        │  │10.0.0.1 │ │192.168.4│  │  │
│  │  └──────────────┘  │        │  └────────┘ └─────────┘  │  │
│  │  ┌──────────────┐  │        │       ↕  IP_FORWARD  ↕    │  │
│  │  │  CDC-ACM     │  │        │       nat.c  (NAPT)        │  │
│  │  │  (Serial)    │  │        │  LWIP_HOOK_IP4_INPUT       │  │
│  │  └──────────────┘  │        └──────────────────────────┘  │
│  └────────────────────┘                    │                  │
│           │ USB                     CYW43 driver              │
│           │                               │ WiFi              │
└───────────┼───────────────────────────────┼──────────────────┘
            │                               │
       [USB Host]                    [WiFi Clients]
        10.0.0.2                      192.168.4.10
```

---

## Layer Breakdown

### TinyUSB (USB Device Stack)

**Role:** Presents the Pico W as a composite USB device with two interfaces.

| Interface | Class | Function |
|-----------|-------|----------|
| CDC-NCM (IAD) | Communications | USB Ethernet NIC (CDC Network Control Model) |
| CDC-ACM | Communications | Debug serial console (`printf` → `/dev/ttyACM*`) |

CDC-NCM was chosen over RNDIS because:
- CDC-NCM is a proper IEEE standard, supported plug-and-play on Linux, macOS, and Windows 10+
- RNDIS requires drivers on older Windows versions and is a Microsoft proprietary protocol

The composite device uses Interface Association Descriptors (IAD) so the OS correctly groups CDC-NCM's two interfaces (Communication + Data) into a single logical device.

**Key callback — `tud_network_recv_cb()`**: Called when TinyUSB receives an Ethernet frame over USB. We pass it directly to lwIP via `usb_netif->input()`.

**Key callback — `tud_network_xmit_cb()`**: Called by TinyUSB when it's ready to send a queued frame. We copy the pbuf payload into the USB transfer buffer.

### lwIP (TCP/IP Stack)

**Role:** Full IP networking, routing, DHCP, and NAT.

Configuration: `NO_SYS=1` (no RTOS, cooperative poll mode). All drivers are polled from the main loop — no interrupts, no threads.

**Two network interfaces:**

| Netif | Name | IP | Driver |
|-------|------|----|--------|
| USB | `us` | `10.0.0.1/24` | `usb_net.c` |
| WiFi | `w1` | `192.168.4.1/24` | CYW43 (`pico_cyw43_arch_lwip_poll`) |

`IP_FORWARD=1` enables packet forwarding between interfaces. When a packet arrives on one interface and is addressed to a host reachable via another, `ip4_forward()` routes it.

**DHCP servers:** Two separate DHCP server instances (one per interface), both using `dhcpserver.c` with fixed IP assignment.

### nat.c — Bidirectional NAPT

**Role:** Network Address and Port Translation between the two subnets.

lwIP's built-in NAPT (`ip4_napt.c`) is not included in the Pico SDK's lwIP snapshot (it was added post-2.1.x upstream). We implement a minimal but complete NAPT in `src/nat.c`.

The NAT hook is registered via `LWIP_HOOK_IP4_INPUT` in `lwipopts.h`:

```c
#define LWIP_HOOK_IP4_INPUT(p, inp) nat_ip4_input_hook(p, inp)
```

This hook is called at `ip4.c:491`, before `ip_data.current_iphdr_dest` is copied at line 549. This ordering is critical: the hook can rewrite `iphdr->dest.addr`, and the subsequent routing decision will use the rewritten destination.

---

## NAT Data Flow

### 4 Translation Functions

| Trigger | Function | Action |
|---------|----------|--------|
| pkt arrives on USB, dst ∈ WiFi subnet | `nat_outbound` | USB client src → Pico WiFi IP (`192.168.4.1`), allocate NAT port |
| pkt arrives on WiFi, dst = Pico WiFi IP | `nat_inbound` | restore original USB client IP from NAT table |
| pkt arrives on WiFi, dst ∈ USB subnet | `nat_outbound2` | WiFi client src → Pico USB IP (`10.0.0.1`), allocate NAT port |
| pkt arrives on USB, dst = Pico USB IP | `nat_inbound2` | rewrite dst to original WiFi client IP, let lwIP forward to WiFi |

### Packet Flow: SSH from WiFi client → USB server

```
WiFi client (192.168.4.10) sends:
  src=192.168.4.10:52000 dst=10.0.0.2:22 proto=TCP

nat_outbound2():
  new src = 10.0.0.1:49152 (Pico USB IP, NAT port)
  NAT table: {orig_src=192.168.4.10, orig_sport=52000, nat_port=49152}

Forwarded to USB server (10.0.0.2):
  src=10.0.0.1:49152 dst=10.0.0.2:22

USB server replies:
  src=10.0.0.2:22 dst=10.0.0.1:49152

nat_inbound2():
  lookup nat_port=49152 → orig_src=192.168.4.10, orig_sport=52000
  rewrite dst: 10.0.0.1 → 192.168.4.10
  lwIP routes to WiFi interface

WiFi client receives:
  src=10.0.0.2:22 dst=192.168.4.10:52000  ✓
```

### NAT Table Structure

```c
typedef struct {
    uint32_t  orig_src;      // original source IP (host byte order)
    uint32_t  orig_dst;      // original destination IP
    uint16_t  orig_sport;    // original source port / ICMP ID (BE read)
    uint16_t  orig_dport;    // original destination port
    uint16_t  nat_port;      // allocated NAT port (host byte order, range 49152-65535)
    uint8_t   proto;         // IP_PROTO_TCP / IP_PROTO_UDP / IP_PROTO_ICMP
    bool      active;
    uint32_t  last_seen_ms;  // for TTL expiry
} nat_entry_t;
```

Table size: `NAT_TABLE_SIZE` (default 64 entries). TTLs: TCP 120s, UDP 30s, ICMP 10s.

---

## Checksum Architecture

### The Problem

`ip4_forward()` in lwIP (ip4.c:332–366) zeroes all checksums when `CHECKSUM_GEN_*=1`:

```c
// ip4.c:340 — when CHECKSUM_GEN_IP=1
IPH_CHKSUM_SET(iphdr, 0);  // zero IP checksum (HW offload expected)
// same for TCP, UDP, ICMP
```

This assumes the transmitting NIC will compute checksums in hardware (checksum offload). Neither TinyUSB's CDC-NCM driver nor the CYW43439 WiFi driver implement hardware checksum offload.

### The Fix: Software Checksum Engines

Two software checksum wrappers, one per interface:

**USB side (`usb_net.c`):** `usb_fill_checksums()` is called inside `usb_netif_linkoutput()` before each frame is handed to TinyUSB.

**WiFi side (`network.c`):** After `wifi_ap_init()` sets up the CYW43 AP, `network_init()` saves the original `wifi->linkoutput` pointer and replaces it with `wifi_linkoutput_chksum()`, which calls `wifi_fill_checksums()` then delegates to the original CYW43 linkoutput.

Both engines implement the same logic:
1. Check `ethertype == 0x0800` (IPv4)
2. Recompute IP header checksum (`inet_chksum`)
3. Recompute transport checksum based on protocol:
   - **ICMP**: `inet_chksum(icmph, icmp_payload_len)`
   - **TCP**: `inet_chksum_pseudo()` (includes IP pseudo-header)
   - **UDP**: `inet_chksum_pseudo()` (skip if checksum field is 0)

`LWIP_NETIF_TX_SINGLE_PBUF=1` guarantees single-segment pbufs on transmit, so no chained-pbuf traversal is needed.

---

## Cooperative Poll Loop

No RTOS. The main loop polls all drivers:

```c
while (true) {
    tud_task();            // TinyUSB: USB enumeration, NCM rx/tx
    cyw43_arch_poll();     // CYW43: WiFi driver + lwIP rx/tx
    sys_check_timeouts();  // lwIP: TCP keepalive, ARP, DHCP renewal
    status_task();         // periodic UART status (every 15 s)
}
```

**Critical constraint:** Do NOT call `tud_task()` inside `usb_netif_linkoutput()`.

The call chain `tud_task() → tud_network_recv_cb() → DHCP handler → lwIP output → usb_netif_linkoutput()` is normal. Calling `tud_task()` again from within `linkoutput` would cause re-entrant TinyUSB calls that corrupt its internal DMA state.

When the USB TX buffer is full (`tud_network_can_xmit()` returns false), `linkoutput` returns `ERR_BUF`. Higher-level protocols (TCP, DHCP) handle retransmission.

---

## DHCP Option 121 (Classless Static Routes)

RFC 3442 defines DHCP option 121, which tells DHCP clients to install static routes. We use this to automatically push `192.168.4.0/24 via 10.0.0.1` into the USB host's routing table.

Without this, a Linux host with multiple NICs would not know to route WiFi subnet traffic through the Pico's USB interface, requiring a manual `ip route add` after every reboot.

The option encoding (simplified for `/24`):

```
Option code: 121
Length: 6 (1 prefix_len + 3 significant octets + 4 gateway octets - wait, /24 = 3 sig octets)
  Actually: 1 + ceil(24/8) + 4 = 1 + 3 + 4 = 8 bytes
Content: [24] [192] [168] [4] [10] [0] [0] [1]
         ^^^   ^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^
        prefix  network octets     gateway IP
```

Linux systemd-networkd and dhclient both honor option 121. The route appears with `proto dhcp` and is managed (renewed/removed) alongside the DHCP lease.

---

## USB Descriptors

The device presents as a composite device (bDeviceClass=0xEF, IAD). String descriptor index 6 carries the CDC-NCM MAC address in ASCII:

```
MAC: 02:02:84:69:60:00  →  string: "020284696000"
```

This MAC is also copied into `tud_network_mac_address[6]` (the Pico's "remote" MAC as seen by the host). The local MAC (host side) is negotiated by the OS.

The MAC prefix `02:xx` sets the locally-administered bit, avoiding conflict with registered OUI ranges.

---

## Why No RTOS?

TinyUSB and lwIP both provide cooperative poll modes that compose cleanly:

- `tud_task()` processes all pending USB events in one call and returns
- `cyw43_arch_poll()` (in `lwip_poll` mode) processes WiFi events and lwIP timers
- `sys_check_timeouts()` fires any pending lwIP timer callbacks

Adding an RTOS (FreeRTOS) would require:
- Thread-safe access to the lwIP core (mutex or `LWIP_TCPIP_CORE_LOCKING=1`)
- Careful synchronization between TinyUSB's USB ISR and lwIP's network callbacks
- Additional RAM overhead (~4 KB minimum for FreeRTOS kernel)

For this use case — a small bridge device with no complex application logic — cooperative polling provides simpler, more predictable behavior with lower overhead.
