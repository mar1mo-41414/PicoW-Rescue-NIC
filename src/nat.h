#pragma once

#include <stdint.h>
#include "lwip/pbuf.h"
#include "lwip/netif.h"

// ---------------------------------------------------------------------------
// Minimal NAPT for PicoW-NIC
// Masquerades traffic from USB (10.0.0.0/24) going out via WiFi AP.
// Intercepts forwarded packets at the IP layer using the lwIP hook mechanism.
//
// Call nat_init() after both netifs are up.
// ---------------------------------------------------------------------------

#define NAT_TABLE_SIZE  64          // max simultaneous sessions
#define NAT_TCP_TTL_MS  30000u      // 30 s idle timeout for TCP
#define NAT_UDP_TTL_MS  20000u      // 20 s for UDP
#define NAT_ICMP_TTL_MS 10000u      // 10 s for ICMP

void nat_init(void);

// Called from LWIP_HOOK_IP4_INPUT — returns 1 if packet was consumed/modified.
// lwIP will skip normal input processing when this returns 1.
// For forwarded packets that need NAT rewrite, we modify in-place and
// re-inject via ip4_input() on the correct interface.
int nat_ip4_input_hook(struct pbuf *p, struct netif *inp);
