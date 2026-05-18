# Troubleshooting Guide — PicoW-Rescue-NIC

---

## Quick Diagnostic Checklist

1. Does the USB interface appear on the server? (`ip addr` shows `10.0.0.2/24`)
2. Does the server have the WiFi route? (`ip route` shows `192.168.4.0/24 via 10.0.0.1`)
3. Can the server ping the Pico? (`ping 10.0.0.1`)
4. Can the WiFi client ping the Pico? (`ping 192.168.4.1`)
5. Can the WiFi client ping the server? (`ping 10.0.0.2`)

If step 1–4 pass but step 5 fails, the issue is in NAT or routing. Enable verbose logging to diagnose.

---

## Problem: Pico Not Appearing as a USB Network Interface

### Symptom
`ip addr` shows no new network interface after plugging in the Pico.

### Possible Causes & Fixes

**The Pico is not running the firmware**
- Check: LED should be solid on (WiFi AP active). If LED is off or blinking, the firmware may not be flashed.
- Fix: Re-flash. Hold BOOTSEL, plug USB, copy `.uf2` to the RPI-RP2 drive.

**CDC-NCM not supported by OS**
- Windows 7/8 and some older Windows 10 builds may not support CDC-NCM without drivers.
- Fix: Use Windows 10 version 1903+ or Windows 11. For older Windows, RNDIS support is on the roadmap.

**USB cable issue**
- Charge-only cables have no data lines.
- Fix: Use a known-good data cable.

**Missing kernel module (Linux)**
```bash
sudo modprobe cdc_ncm
lsmod | grep cdc_ncm
```

---

## Problem: Got USB Interface But No IP Address

### Symptom
`ip addr` shows the interface (e.g., `picow`, `usb0`, `enp0s26u1u1u4`) but no IPv4 address.

### Possible Causes & Fixes

**DHCP client not running**
```bash
sudo dhclient picow          # or your interface name
# or with systemd-networkd:
sudo networkctl               # check if interface is managed
```

**Interface name differs from expected**
```bash
ip link | grep -E "usb|picow|enp.*u"
```
Use the actual interface name for DHCP.

**Pico's DHCP server not ready yet**
- The Pico needs ~2 seconds to initialize WiFi AP before USB DHCP is ready.
- Fix: Unplug and replug the USB after 3 seconds.

---

## Problem: USB IP Works But No Route to WiFi Subnet

### Symptom
`ping 10.0.0.1` succeeds but `ping 192.168.4.10` fails with "Network unreachable."

### Cause
The WiFi subnet route was not installed. This should happen automatically via DHCP option 121.

### Fix

**Check if the route was pushed:**
```bash
ip route | grep 192.168.4
# Expected: 192.168.4.0/24 via 10.0.0.1 dev picow proto dhcp
```

**If not present, force a DHCP renew:**
```bash
sudo dhclient -r picow && sudo dhclient picow
```

**If still not present (DHCP client doesn't support option 121):**
```bash
sudo ip route add 192.168.4.0/24 via 10.0.0.1
```

**Note:** Some minimal DHCP clients (e.g., `busybox udhcpc`) may not process option 121. `systemd-networkd`, `NetworkManager`, and `dhclient` all support it.

---

## Problem: Ping Works But SSH Fails

### Symptom
`ping 10.0.0.2` works from the WiFi client, but `ssh user@10.0.0.2` times out or connection is refused.

### Possible Causes & Fixes

**SSH not running on the server**
```bash
# On the server (via USB or local access):
systemctl status ssh
sudo systemctl start ssh
```

**iptables/nftables blocking the connection**
```bash
# On the server — that's why you're using the Pico! If SSH is blocked:
sudo iptables -I INPUT -i picow -j ACCEPT   # or your USB interface name
# Or flush all rules (careful on a production system):
sudo iptables -F
```

**NAT table issue — enable verbose logging**
```c
// In lwipopts.h, set to 1 and rebuild:
#define VERBOSE_LOG  1
```
Connect to the debug console and look for:
```
NAT TCP: inp=w1 src=192.168.4.10:XXXXX dst=10.0.0.2:22
```
If this line appears, the outbound NAT is working. If the reply SYN-ACK never triggers an inbound NAT log, check the return path.

---

## Problem: Only One WiFi Client Can Connect

### Symptom
A second device cannot connect to the `PicoBridge` WiFi, or connects but gets no IP address.

### Cause
The DHCP server assigns a single fixed IP (`192.168.4.10`). Only one DHCP lease is supported. Multiple WiFi connections to the AP are possible, but only one will receive an IP.

### Workaround
Connect the second device manually with a static IP:
```
IP: 192.168.4.11
Subnet: 255.255.255.0
Gateway: 192.168.4.1
```

### Fix (Future)
Expand `dhcpserver.c` to support a lease table. Filed as a roadmap item.

---

## Problem: Connection Works Initially But Drops After a While

### Symptom
SSH or ping works for a minute or two, then stalls or drops.

### Possible Causes & Fixes

**NAT table timeout**
Default TTL: TCP 120s, UDP 30s, ICMP 10s. Long-idle connections may be evicted.

For SSH keep-alive, add to `~/.ssh/config`:
```
Host *
    ServerAliveInterval 30
    ServerAliveCountMax 3
```

**NAT table full**
If many connections are open simultaneously, the 64-entry table may exhaust.
```c
// nat.h — increase the table size:
#define NAT_TABLE_SIZE 128
```

**WiFi interference**
The Pico's AP may lose association with a client in noisy RF environments.
Check the debug console for RSSI in the status output.

---

## Problem: Debug Console Shows Nothing

### Symptom
Opening `/dev/ttyACM0` shows no output.

### Possible Causes & Fixes

**Wrong device file**
```bash
ls /dev/ttyACM*          # Linux
ls /dev/cu.usbmodem*     # macOS
```
There may be multiple ACM devices if other USB serial devices are connected.

**Terminal not opened before Pico boot**
The 5-second boot window waits for a CDC connection before printing the banner. If you open the terminal after that window, you missed the banner — but status lines appear every 15 seconds.

**Baud rate matters for some terminals**
Use 115200 baud, 8N1. This is the standard and should work with `screen`, `picocom`, or PuTTY.

```bash
screen /dev/ttyACM0 115200
# or
picocom -b 115200 /dev/ttyACM0
```

---

## Problem: Packets Are Forwarded But Checksums Are Wrong

### Symptom
`tcpdump -vv` on either interface shows `bad cksum` warnings. Packets are dropped by the destination.

### Cause
This should be fixed in v1.0. If you see this, it means the software checksum engines are not running.

### Diagnosis
```bash
sudo tcpdump -i picow -n -vv icmp
# Look for: ICMP echo reply, bad cksum 0 (->xxxx)!
```

### Fix
Verify `lwipopts.h` has:
```c
#define CHECKSUM_GEN_IP     1
#define CHECKSUM_GEN_TCP    1
#define CHECKSUM_GEN_UDP    1
#define CHECKSUM_GEN_ICMP   0   // must be 0 — ip4_forward zeros this otherwise
```

And that `usb_fill_checksums()` is being called in `usb_netif_linkoutput()`, and `wifi_fill_checksums()` is being called via the linkoutput wrapper in `network_init()`.

See [`docs/investigation.md`](investigation.md) for the full checksum bug analysis.

---

## Enabling Verbose Per-Packet Logs

For detailed debugging, enable verbose logging:

```c
// lwipopts.h
#define VERBOSE_LOG  1   // set to 1, rebuild and reflash
```

This enables per-packet output on the debug console:

```
USB RX: 98 bytes  IPv4 proto=1 dport=12345
NAT ICMP: inp=w1 src=192.168.4.10 dst=10.0.0.2 type=8
USB TX: 98 bytes  ready=1 can_xmit=1
```

**Log prefixes:**
- `USB RX:` — packet received from USB host
- `USB TX:` — packet being sent to USB host
- `NAT ICMP/TCP/UDP:` — NAT table lookup/rewrite
- `DHCP recv:` — DHCP packet handling

Set back to `0` for normal use — verbose mode prints ~10 lines per ping packet and will slow things down.

---

## Reflashing the Firmware

```bash
# Method 1: UF2 drag-and-drop
# 1. Unplug Pico
# 2. Hold BOOTSEL button
# 3. Plug USB — "RPI-RP2" drive appears
# 4. Release button
cp build/picow_nic.uf2 /media/$USER/RPI-RP2/
# Pico reboots automatically

# Method 2: picotool (if installed)
picotool load build/picow_nic.uf2 --force
picotool reboot

# Method 3: OpenOCD (SWD)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "program build/picow_nic.elf verify reset exit"
```

---

## Still Stuck?

1. Check [`docs/investigation.md`](investigation.md) — it documents all bugs found during development
2. Enable `VERBOSE_LOG=1`, capture the debug console output, and open an issue on GitHub
3. Include: OS version, `ip addr`, `ip route`, `dmesg | tail -20`, and the Pico debug console log
