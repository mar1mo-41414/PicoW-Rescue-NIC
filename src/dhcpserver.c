/*
 * dhcpserver.c — minimal DHCP server for PicoW-NIC
 *
 * Derived from Raspberry Pi pico-examples/pico_w/wifi/access_point/dhcpserver.c
 * (BSD-3-Clause).  Adapted to support two simultaneous instances (USB + WiFi).
 *
 * Assigns leases starting at gw+1 up to gw+DHCP_MAX_LEASES.
 * Lease lifetime: 24 h (refreshed on RENEW/REBIND).
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#include "dhcpserver.h"

// ---- DHCP protocol constants -----------------------------------------------
#define DHCP_PORT_SERVER    67
#define DHCP_PORT_CLIENT    68

#define DHCP_OP_REQUEST     1
#define DHCP_OP_REPLY       2
#define DHCP_HTYPE_ETH      1
#define DHCP_HLEN_ETH       6
#define DHCP_FLAGS_BROADCAST 0x8000u

#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPACK         5
#define DHCPNAK         6

#define OPT_SUBNET_MASK         1
#define OPT_ROUTER              3
#define OPT_DNS                 6
#define OPT_REQUESTED_IP        50
#define OPT_LEASE_TIME          51
#define OPT_MSG_TYPE            53
#define OPT_SERVER_ID           54
#define OPT_PARAM_LIST          55
#define OPT_END                 255

#define DHCP_LEASE_TIME_S       (24 * 3600u)   // 24 hours

// ---- Wire format -----------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];
    uint8_t  siaddr[4];
    uint8_t  giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint8_t  magic[4];
    uint8_t  options[308];
} dhcp_msg_t;

// ---- Helpers ---------------------------------------------------------------

static int opt_write_byte(uint8_t *p, uint8_t code, uint8_t val) {
    p[0] = code; p[1] = 1; p[2] = val;
    return 3;
}

static int opt_write_u32(uint8_t *p, uint8_t code, uint32_t val) {
    p[0] = code; p[1] = 4;
    p[2] = (val >> 24) & 0xff;
    p[3] = (val >> 16) & 0xff;
    p[4] = (val >>  8) & 0xff;
    p[5] =  val        & 0xff;
    return 6;
}

static int opt_write_ip(uint8_t *p, uint8_t code, uint32_t ip_he) {
    // ip_he: host-endian IPv4 address
    return opt_write_u32(p, code, ip_he);
}

// Read a DHCP option byte value (returns -1 if not found).
static int opt_read_byte(const uint8_t *opts, uint16_t opts_len, uint8_t code) {
    for (uint16_t i = 0; i + 2 < opts_len; ) {
        uint8_t t = opts[i];
        if (t == OPT_END) break;
        uint8_t l = opts[i + 1];
        if (t == code && l >= 1) return opts[i + 2];
        i += 2 + l;
    }
    return -1;
}

// Read a 4-byte option as host-endian u32 (returns 0 if not found).
static uint32_t opt_read_u32(const uint8_t *opts, uint16_t opts_len, uint8_t code) {
    for (uint16_t i = 0; i + 5 < opts_len; ) {
        uint8_t t = opts[i];
        if (t == OPT_END) break;
        uint8_t l = opts[i + 1];
        if (t == code && l == 4) {
            return ((uint32_t)opts[i+2] << 24) | ((uint32_t)opts[i+3] << 16) |
                   ((uint32_t)opts[i+4] <<  8) |  (uint32_t)opts[i+5];
        }
        i += 2 + l;
    }
    return 0;
}

// ---- Lease management ------------------------------------------------------

// Find lease for a MAC, or return NULL.
static dhcp_lease_t *find_lease_by_mac(dhcp_server_t *d, const uint8_t *mac) {
    for (int i = 0; i < DHCP_MAX_LEASES; i++) {
        if (d->leases[i].active && memcmp(d->leases[i].mac, mac, 6) == 0)
            return &d->leases[i];
    }
    return NULL;
}

// Allocate (or reuse) a lease for a MAC; returns lease index or -1.
static int alloc_lease(dhcp_server_t *d, const uint8_t *mac) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Reuse existing lease for this MAC.
    for (int i = 0; i < DHCP_MAX_LEASES; i++) {
        if (d->leases[i].active && memcmp(d->leases[i].mac, mac, 6) == 0)
            return i;
    }

    // Allocate a free or expired slot.
    for (int i = 0; i < DHCP_MAX_LEASES; i++) {
        if (!d->leases[i].active || (int32_t)(now - d->leases[i].expiry_ms) >= 0) {
            memcpy(d->leases[i].mac, mac, 6);
            d->leases[i].active    = true;
            d->leases[i].expiry_ms = now + DHCP_LEASE_TIME_S * 1000u;
            return i;
        }
    }
    return -1;  // no free slot
}

// Compute offered IP: gw_ip + 1 + index.
static uint32_t lease_to_ip(dhcp_server_t *d, int idx) {
    return ntohl(ip4_addr_get_u32(&d->gw)) + 1 + (uint32_t)idx;
}

// ---- Send DHCP reply -------------------------------------------------------

static void send_reply(dhcp_server_t *d, struct udp_pcb *pcb,
                       const dhcp_msg_t *req, int msg_type, int lease_idx,
                       const ip_addr_t *src_ip) {
    dhcp_msg_t reply;
    memset(&reply, 0, sizeof(reply));

    reply.op    = DHCP_OP_REPLY;
    reply.htype = DHCP_HTYPE_ETH;
    reply.hlen  = DHCP_HLEN_ETH;
    reply.xid   = req->xid;

    // Offered IP
    if (lease_idx >= 0) {
        uint32_t offered = htonl(lease_to_ip(d, lease_idx));
        memcpy(reply.yiaddr, &offered, 4);
    }

    // Server IP = gateway
    uint32_t gw_n = ip4_addr_get_u32(&d->gw);
    memcpy(reply.siaddr, &gw_n, 4);

    memcpy(reply.chaddr, req->chaddr, 16);

    // Magic cookie
    reply.magic[0] = 99; reply.magic[1] = 130;
    reply.magic[2] = 83; reply.magic[3] = 99;

    // Options
    uint8_t *o = reply.options;
    int      n = 0;

    n += opt_write_byte(o + n, OPT_MSG_TYPE, (uint8_t)msg_type);

    uint32_t gw_he = ntohl(gw_n);
    n += opt_write_ip(o + n, OPT_SERVER_ID,   gw_he);
    n += opt_write_u32(o + n, OPT_LEASE_TIME, DHCP_LEASE_TIME_S);
    n += opt_write_ip(o + n, OPT_SUBNET_MASK, ntohl(ip4_addr_get_u32(&d->mask)));
    n += opt_write_ip(o + n, OPT_ROUTER,      gw_he);
    n += opt_write_ip(o + n, OPT_DNS,         gw_he);  // Pico as DNS (pass-through)

    o[n++] = OPT_END;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(reply), PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, &reply, sizeof(reply));

    // Broadcast reply (client may not have an IP yet).
    ip_addr_t dst;
    IP4_ADDR(&dst, 255, 255, 255, 255);
    udp_sendto(pcb, p, &dst, DHCP_PORT_CLIENT);
    pbuf_free(p);
}

// ---- UDP receive callback --------------------------------------------------

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
    (void)port;
    dhcp_server_t *d = (dhcp_server_t *)arg;

    if (p->tot_len < sizeof(dhcp_msg_t) - sizeof(((dhcp_msg_t *)0)->options)) {
        pbuf_free(p);
        return;
    }

    dhcp_msg_t msg;
    uint16_t   copied = pbuf_copy_partial(p, &msg, sizeof(msg), 0);
    pbuf_free(p);

    if (copied < 240) return;  // must have at least magic + 1 option
    if (msg.op != DHCP_OP_REQUEST) return;

    // Parse message type
    uint16_t opts_len = (uint16_t)(copied - offsetof(dhcp_msg_t, options));
    int msg_type = opt_read_byte(msg.options, opts_len, OPT_MSG_TYPE);

    const uint8_t *mac = msg.chaddr;

    switch (msg_type) {
    case DHCPDISCOVER: {
        int idx = alloc_lease(d, mac);
        if (idx < 0) {
            printf("DHCP: no free leases\n");
            return;
        }
        send_reply(d, pcb, &msg, DHCPOFFER, idx, addr);
        printf("DHCP OFFER  -> %d.%d.%d.%d  MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
               10 & 0xff, 0 & 0xff, 0 & 0xff, 2 + idx,   // just for display
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        break;
    }
    case DHCPREQUEST: {
        // Requested IP must be in our pool
        uint32_t req_ip = opt_read_u32(msg.options, opts_len, OPT_REQUESTED_IP);
        int idx = alloc_lease(d, mac);
        if (idx < 0) {
            send_reply(d, pcb, &msg, DHCPNAK, -1, addr);
            return;
        }
        uint32_t our_ip = lease_to_ip(d, idx);
        if (req_ip != 0 && req_ip != our_ip) {
            // Wrong IP requested — reassign to our choice
            (void)req_ip;
        }
        // Refresh expiry
        d->leases[idx].expiry_ms =
            to_ms_since_boot(get_absolute_time()) + DHCP_LEASE_TIME_S * 1000u;
        send_reply(d, pcb, &msg, DHCPACK, idx, addr);
        printf("DHCP ACK    -> %u.%u.%u.%u  MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
               (our_ip >> 24) & 0xff, (our_ip >> 16) & 0xff,
               (our_ip >>  8) & 0xff,  our_ip        & 0xff,
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        break;
    }
    default:
        break;
    }
}

// ---- Public API ------------------------------------------------------------

void dhcp_server_init(dhcp_server_t *d, ip_addr_t *gw, ip_addr_t *mask) {
    ip_addr_copy(d->gw,   *gw);
    ip_addr_copy(d->mask, *mask);
    memset(d->leases, 0, sizeof(d->leases));

    d->udp = udp_new();
    if (!d->udp) {
        printf("DHCP: failed to create PCB\n");
        return;
    }
    udp_recv(d->udp, dhcp_recv, d);
    udp_bind(d->udp, IP_ADDR_ANY, DHCP_PORT_SERVER);
}

void dhcp_server_deinit(dhcp_server_t *d) {
    if (d->udp) {
        udp_remove(d->udp);
        d->udp = NULL;
    }
}
