# Investigation Log — PicoW-Rescue-NIC

This document chronicles the debugging journey that led to the v1.0 firmware. Each bug is documented with its symptoms, root cause analysis, and fix. Preserved for contributors who encounter similar issues.

---

## Bug 1: USB TX Checksums Are Zero

### Symptoms

After the Pico enumerates as a USB CDC-NCM NIC and Linux obtains a DHCP lease, all ICMP/TCP/UDP packets received by Linux from the Pico have a checksum of zero.

```bash
sudo tcpdump -i picow -n icmp -vv
# Output:
# 192.168.4.1 > 10.0.0.2: ICMP echo reply, bad cksum 0 (->a3c1)!
```

Linux drops these silently. No ping responses visible.

### Root Cause

`ip4_forward()` in lwIP (`lwip/src/core/ipv4/ip4.c`, around line 332–366) zeroes all checksum fields when `CHECKSUM_GEN_IP / CHECKSUM_GEN_TCP / CHECKSUM_GEN_UDP = 1`:

```c
// ip4.c:340
if (CHECKSUM_GEN_IP || NETIF_CHECKSUM_ENABLED(inp, NETIF_CHECKSUM_GEN_IP)) {
    IPH_CHKSUM_SET(iphdr, 0);   // zero IP checksum — HW fills it in
}
// same pattern for TCP header checksum, UDP checksum, ICMP checksum
```

This is intentional design: when a NIC supports hardware checksum offload, the driver fills in checksums after the stack hands off the packet. TinyUSB's CDC-NCM implementation has no such mechanism — it transmits exactly what it receives.

### Fix

In `usb_net.c`, intercept every packet in `usb_netif_linkoutput()` and recompute all checksums in software before handing to TinyUSB:

```c
static err_t usb_netif_linkoutput(struct netif *netif, struct pbuf *p) {
    if (!tud_ready())                       return ERR_USE;
    if (!tud_network_can_xmit(p->tot_len))  return ERR_BUF;
    usb_fill_checksums(p);   // recompute IP/ICMP/TCP/UDP checksums
    tud_network_xmit(p, 0);
    return ERR_OK;
}
```

`usb_fill_checksums()` handles IPv4 only (checks `ethertype == 0x0800`), recomputes IP header checksum first, then transport checksum (ICMP, TCP, or UDP) using `inet_chksum` / `inet_chksum_pseudo`. Single-segment pbufs are guaranteed by `LWIP_NETIF_TX_SINGLE_PBUF=1`.

---

## Bug 2: WiFi TX Checksums Are Zero (Mac Drops Packets)

### Symptoms

After fixing Bug 1, ping from Mac → Linux works. But Linux → Mac fails: the Mac receives packets silently but drops them. `tcpdump` on the Mac shows checksums of zero.

### Root Cause

Same root cause as Bug 1, but for the other interface. `ip4_forward()` also zeroes checksums for packets sent out via the WiFi (CYW43) interface. The CYW43439 driver (`cyw43_arch_lwip_poll`) likewise does not implement hardware checksum offload.

Bug 1's fix only handled the USB linkoutput path. The WiFi linkoutput path was still unpatched.

### Fix

In `network_init()` (called at startup), wrap the WiFi netif's `linkoutput` function pointer:

```c
// network.c
static netif_linkoutput_fn wifi_orig_linkoutput = NULL;

static err_t wifi_linkoutput_chksum(struct netif *netif, struct pbuf *p) {
    wifi_fill_checksums(p);                  // recompute before transmitting
    return wifi_orig_linkoutput(netif, p);   // delegate to CYW43 driver
}

void network_init(void) {
    nat_init();
    struct netif *wifi = wifi_ap_get_netif();
    if (wifi && wifi->linkoutput) {
        wifi_orig_linkoutput = wifi->linkoutput;
        wifi->linkoutput = wifi_linkoutput_chksum;
    }
}
```

This wrapping must happen _after_ `wifi_ap_init()` has called `cyw43_arch_enable_ap_mode()`, which is when the CYW43 driver installs its own `linkoutput` pointer. If wrapped too early, `wifi->linkoutput` is NULL.

---

## Bug 3: ICMP Echo Reply Has Wrong ID (Double Byte-Swap)

### Symptoms

After fixing checksums, Mac → Linux ping works. But the ICMP echo reply from Linux never matches the request ID at the Mac, so the Mac's `ping` reports 100% packet loss even though packets are flowing.

Observed in verbose log:
```
NAT ICMP: inp=us src=10.0.0.2 dst=10.0.0.1 type=0
USB TX: 78 bytes
```
The packets arrive at the Mac, but `ping` discards them ("wrong ID").

### Root Cause

In `nat_inbound()` and `nat_inbound2()`, the ICMP echo ID restoration was written as:

```c
uint16_t new_id = htons(e->orig_sport);   // BUG
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);
```

`e->orig_sport` was stored from the packet bytes as `(tp[4] << 8) | tp[5]` — this is already in "big-endian integer" form (the numeric value of the 2-byte field when read MSB-first). Writing `new_id >> 8` and `new_id & 0xff` back to `tp[4]/tp[5]` correctly reconstructs the original byte sequence.

Applying `htons()` on a little-endian CPU swaps the bytes, producing the wrong ID in the packet.

### Fix

Remove `htons()`:

```c
uint16_t new_id = e->orig_sport;   // already in the right byte order
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);
```

Applied in both `nat_inbound` and `nat_inbound2`.

---

## Bug 4: TCP/UDP NAT Lookup Fails (SSH Broken)

### Symptoms

After fixing Bug 3, ping works in both directions. But SSH and curl (TCP) still fail — connections time out immediately.

Verbose log shows the SYN packet arriving at the Pico and being NATed outbound, but the SYN-ACK from the server triggers no inbound NAT lookup (the packet is dropped or sent to the wrong destination).

### Root Cause

In `nat_inbound()` and `nat_inbound2()`, the `find_inbound()` call was:

```c
nat_entry_t *e = find_inbound(orig_dst_he, ntohs(dport), proto);  // BUG
```

TCP/UDP destination port is read from the packet as `dport = (tp[2] << 8) | tp[3]`, which yields the same integer as `e->nat_port` (allocated by `alloc_port()` as a plain host-order value in range 49152–65535).

Applying `ntohs()` on a little-endian CPU swaps the bytes. For example:

```
nat_port = 49152  (0xC000)
TCP packet bytes: [0xC0, 0x00] → dport = 0xC000 = 49152
ntohs(49152) on LE = 0x00C0 = 192   ← does not match nat_port=49152!
```

**Why ICMP worked despite this bug:** In the outbound path (`nat_outbound`), the ICMP ID was being written with `htons(nat_port)` — accidentally producing the wrong byte order. When the reply came back, its ID bytes were `[0x00, 0xC0]` = `dport = 0x00C0 = 192`. Then `ntohs(192) = 0xC000 = 49152 = nat_port` — the two bugs cancelled each other out for ICMP.

TCP had no such coincidental cancellation, so it failed.

### Fix

Two-part fix to establish a consistent convention:

**Part 1 — Remove `ntohs()` from `find_inbound` calls:**

```c
// nat_inbound() and nat_inbound2():
nat_entry_t *e = find_inbound(orig_dst_he, dport, proto);  // no ntohs
```

**Part 2 — Fix ICMP outbound ID write to match the unified convention:**

```c
// nat_outbound() and nat_outbound2():
// BEFORE (accidentally worked because inbound ntohs compensated):
uint16_t new_id = htons(e->nat_port);

// AFTER (correct: write nat_port directly as big-endian bytes):
uint16_t new_id = e->nat_port;
tp[4] = (uint8_t)(new_id >> 8);
tp[5] = (uint8_t)(new_id & 0xff);
```

After this fix, ICMP/TCP/UDP all use the same lookup convention: `dport` (from packet bytes, read MSB-first) == `nat_port` (allocated as a plain integer).

---

## Bug 5: Linux Needs Manual Route for WiFi Subnet

### Symptoms

On a Linux host with multiple network interfaces (e.g., eth0 + the Pico's `picow` interface), after plugging in the Pico, the host gets a DHCP address (`10.0.0.2/24`) but has no route for `192.168.4.0/24`. Pinging `192.168.4.10` fails with "Network unreachable."

The workaround was:
```bash
sudo ip route add 192.168.4.0/24 via 10.0.0.1
```
But this did not survive reboots and had to be repeated each session.

### Root Cause

The DHCP server was only sending option 3 (default gateway = `10.0.0.1`). Linux with multiple NICs typically does not use a per-interface default gateway for destinations outside that interface's subnet — it uses the kernel routing table. Option 3 alone does not install a host-specific route for the WiFi subnet.

### Fix

Implement DHCP option 121 (Classless Static Route, RFC 3442). This option instructs the DHCP client to install specific static routes, independent of the default gateway.

**dhcpserver.h** — Added route fields to `dhcp_server_t`:
```c
ip4_addr_t  route_net;
uint8_t     route_prefix_len;   // 0 = disabled
ip4_addr_t  route_gw;
```

**dhcpserver.c** — Build option 121 in `send_reply()`:
```c
if (d->route_prefix_len > 0) {
    uint8_t sig = (d->route_prefix_len + 7u) / 8u;  // significant octets
    o[n++] = OPT_CLASSLESS_ROUTE;                    // 121
    o[n++] = (uint8_t)(1u + sig + 4u);              // length
    o[n++] = d->route_prefix_len;                    // prefix bits (24)
    for (uint8_t i = 0; i < sig; i++)
        o[n++] = ip4_addr_get_byte(&d->route_net, i); // 192, 168, 4
    o[n++] = ip4_addr1(&d->route_gw); // 10
    o[n++] = ip4_addr2(&d->route_gw); // 0
    o[n++] = ip4_addr3(&d->route_gw); // 0
    o[n++] = ip4_addr4(&d->route_gw); // 1
}
```

**usb_net.c** — Configure the USB DHCP server to push the WiFi subnet route:
```c
IP4_ADDR(&usb_dhcp.route_net, 192, 168, 4, 0);
usb_dhcp.route_prefix_len = 24;
IP4_ADDR(&usb_dhcp.route_gw,  10, 0, 0, 1);
```

After this fix, on a fresh Linux machine:
```bash
ip route | grep 192.168.4
# → 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp src 10.0.0.2 metric 101
```
The route is installed automatically, managed by the DHCP client, and survives reboots.

---

## Investigation 6: lwIP Hook Ordering vs. ip4_forward

### Question

The NAT hook (`LWIP_HOOK_IP4_INPUT`) rewrites `iphdr->dest.addr`. Does lwIP's routing decision happen before or after the hook? If before, rewriting the destination would be useless.

### Analysis

In `lwip/src/core/ipv4/ip4.c` (`ip4_input` function):

```
Line 491: LWIP_HOOK_IP4_INPUT(p, inp)           ← hook fires here
...
Line 549: ip_addr_copy_from_ip4(                 ← current_iphdr_dest copied HERE
              ip_data.current_iphdr_dest,
              iphdr->dest)
...
Line 577: ip4_input_accept(inp)                  ← "is this for us?" check
Line 652: if (netif == NULL) → ip4_forward()     ← forwarding decision
```

The hook fires at line 491 — **before** `current_iphdr_dest` is set (line 549) and before the accept/forward decision (line 652). Any modification to `iphdr->dest.addr` inside the hook is visible to all subsequent processing.

This is the correct design: `nat_inbound2()` rewrites `iphdr->dest` from `10.0.0.1` (Pico USB IP) to `192.168.4.10` (WiFi client). After the hook returns, lwIP sees the destination as `192.168.4.10`, which is not local to the USB interface, so it calls `ip4_forward()` and routes to the WiFi interface. The WiFi client receives the packet correctly.

### Conclusion

The hook placement in lwIP is intentionally suitable for NAT. No ordering workarounds are required. The hook can freely rewrite source and destination IPs and ports; lwIP will route based on the rewritten values.

---

## Summary of All Fixes

| # | Bug | Symptom | Fix |
|---|-----|---------|-----|
| 1 | USB TX checksum=0 | Linux drops all forwarded packets | `usb_fill_checksums()` in USB linkoutput |
| 2 | WiFi TX checksum=0 | Mac drops all forwarded packets | `wifi_fill_checksums()` wrapping CYW43 linkoutput |
| 3 | ICMP ID double-byte-swap | ICMP replies arrive with wrong ID | Remove `htons()` from inbound ID restoration |
| 4 | TCP/UDP NAT lookup failure | SSH/curl timeout, ICMP-only worked | Remove `ntohs()` from `find_inbound`, fix ICMP outbound write |
| 5 | No WiFi route on Linux | Manual `ip route add` required | DHCP option 121 in `dhcpserver.c` |
| 6 | (n/a) | Hook ordering uncertainty | Confirmed: hook fires before routing decision |
