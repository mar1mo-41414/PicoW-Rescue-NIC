#pragma once

#include "lwip/ip_addr.h"

// ---------------------------------------------------------------------------
// Minimal DHCP server (UDP port 67 → 68)
// Supports up to MAX_DHCP_LEASES simultaneous clients.
// Two independent instances can run on separate netifs (USB + WiFi AP).
// API based on pico-examples/picow_access_point.
// ---------------------------------------------------------------------------

#define DHCP_MAX_LEASES     8

typedef struct {
    uint8_t  mac[6];
    uint32_t expiry_ms;     // absolute timestamp (ms_since_boot)
    bool     active;
} dhcp_lease_t;

typedef struct dhcp_server_t_ {
    ip_addr_t       gw;
    ip_addr_t       mask;
    struct udp_pcb *udp;
    dhcp_lease_t    leases[DHCP_MAX_LEASES];
} dhcp_server_t;

void dhcp_server_init(dhcp_server_t *d, ip_addr_t *gw, ip_addr_t *mask);
void dhcp_server_deinit(dhcp_server_t *d);
