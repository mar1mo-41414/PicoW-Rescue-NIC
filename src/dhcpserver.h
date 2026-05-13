#pragma once

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

// ---------------------------------------------------------------------------
// Minimal DHCP server (UDP port 67 → 68)
// Each instance is bound to a specific netif — critical for multi-netif setups
// where broadcast responses must go out on the correct interface.
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
    struct netif   *netif;          // interface this server is bound to
    dhcp_lease_t    leases[DHCP_MAX_LEASES];
} dhcp_server_t;

// netif: the interface to bind to (responses go out this interface only).
void dhcp_server_init(dhcp_server_t *d, ip_addr_t *gw, ip_addr_t *mask,
                      struct netif *netif);
void dhcp_server_deinit(dhcp_server_t *d);
