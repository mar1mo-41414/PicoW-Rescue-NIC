# Release Notes — v1.0

**Released:** 2026-05-18

## Summary

First stable release of PicoW-Rescue-NIC. All core features are working and tested.

A $6 Raspberry Pi Pico W becomes a plug-and-play emergency network access device: USB Ethernet on one side, WiFi Access Point on the other, bidirectional IP routing/NAPT in between.

---

## What Works

- ✅ **USB Ethernet (CDC-NCM)** — plug-and-play on Linux, macOS, Windows 10+; no drivers needed
- ✅ **WiFi Access Point** — WPA2-AES, configurable SSID/password/channel
- ✅ **Bidirectional NAPT** — ping, SSH, curl, SCP work in both directions (USB↔WiFi)
- ✅ **DHCP option 121** — WiFi subnet route automatically installed on USB host; no `ip route add` required
- ✅ **Debug console** — real-time firmware logs over CDC-ACM serial (same USB cable)
- ✅ **Verbose logging** — per-packet trace mode for debugging (`VERBOSE_LOG=1`)

---

## Bugs Fixed During Development

Six non-trivial bugs were found and fixed before v1.0. See [`docs/investigation.md`](investigation.md) for the full story.

| # | Bug | Impact |
|---|-----|--------|
| 1 | USB TX checksums zeroed by `ip4_forward()` | All USB-side packets dropped by Linux |
| 2 | WiFi TX checksums zeroed by `ip4_forward()` | All WiFi-side packets dropped by Mac |
| 3 | ICMP echo ID double byte-swap in NAT | ICMP replies had wrong ID; ping showed 100% loss |
| 4 | TCP/UDP NAT lookup used wrong byte order | SSH/curl never worked; ICMP only worked by coincidence |
| 5 | No WiFi subnet route on USB host | Manual `ip route add` required after every boot |
| 6 | (investigation) Hook ordering in lwIP | Confirmed NAT hook fires before routing decision |

The root cause of bugs 1–4 is a single lwIP design assumption: `ip4_forward()` zeros checksums expecting hardware offload, which neither TinyUSB CDC-NCM nor CYW43439 provide. The fix is software checksum engines in the linkoutput path for both interfaces.

---

## Known Limitations

- **One WiFi client** — DHCP assigns a single fixed IP. A second device can join with a static IP.
- **IPv4 only** — `LWIP_IPV6=0`
- **USB Full Speed** — 12 Mbps physical, ~2–4 Mbps effective through NAT
- **WiFi→USB direction** — The WiFi client needs a manual route to reach `10.0.0.x` (only USB→WiFi has automatic route distribution)
- **Pico W only** — Tested on RP2040 + CYW43439; Pico 2 W (RP2350) is untested

---

## Roadmap for v1.1+

- [ ] Multiple WiFi clients (DHCP lease table)
- [ ] Station mode (connect to upstream WiFi, bridge to USB)
- [ ] Web status page (lwIP httpd)
- [ ] DNS proxy
- [ ] RNDIS support for older Windows
- [ ] Pico 2 W (RP2350) port

---

## Repository Description

> Emergency USB-WiFi bridge NIC for servers. Plug a $6 Raspberry Pi Pico W into any USB port — get WiFi access to your locked-out machine, no drivers, no cloud.

## Topic Tags

```
raspberry-pi-pico  pico-w  rp2040  tinyusb  lwip  cdc-ncm  wifi
network-bridge  napt  out-of-band  homelab  rescue  embedded-c
```
