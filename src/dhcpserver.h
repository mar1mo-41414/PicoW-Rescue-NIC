#pragma once

#include "lwip/ip_addr.h"
#include "lwip/netif.h"

// ---------------------------------------------------------------------------
// Fixed-IP DHCP server
// Always offers a single pre-configured IP address to any client.
// No lease table — one IP per server instance, given to whoever asks.
// ---------------------------------------------------------------------------

typedef struct dhcp_server_t_ {
    ip_addr_t       gw;
    ip_addr_t       mask;
    ip4_addr_t      fixed_ip;           // the one IP this server always hands out
    struct udp_pcb *udp;
    struct netif   *netif;              // interface to bind/send on
    // Optional DHCP option 121 (Classless Static Route, RFC 3442).
    // Set route_prefix_len > 0 to push one extra static route to clients.
    // e.g. route_net=192.168.4.0, route_prefix_len=24, route_gw=10.0.0.1
    // tells Linux/Mac to route 192.168.4.0/24 via 10.0.0.1 automatically.
    ip4_addr_t      route_net;
    uint8_t         route_prefix_len;   // 0 = disabled
    ip4_addr_t      route_gw;
} dhcp_server_t;

// fixed_ip: the single IP offered to any client (e.g. 10.0.0.2)
// gw/mask:  subnet gateway and netmask sent in the reply options
// netif:    interface to bind/send on — required for multi-netif setups
void dhcp_server_init(dhcp_server_t *d,
                      ip_addr_t *gw, ip_addr_t *mask,
                      const ip4_addr_t *fixed_ip,
                      struct netif *netif);
void dhcp_server_deinit(dhcp_server_t *d);
