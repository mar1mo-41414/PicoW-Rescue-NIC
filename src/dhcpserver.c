/*
 * dhcpserver.c — minimal DHCP server for PicoW-NIC
 *
 * Key fix for multi-netif: bind the UDP socket to a specific interface with
 * udp_bind_netif(), and send responses with udp_sendto_if().  Without this,
 * lwIP routes the broadcast response (255.255.255.255) via the default netif,
 * which may be the wrong interface.
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

#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPACK         5
#define DHCPNAK         6

#define OPT_SUBNET_MASK     1
#define OPT_ROUTER          3
#define OPT_DNS             6
#define OPT_REQUESTED_IP    50
#define OPT_LEASE_TIME      51
#define OPT_MSG_TYPE        53
#define OPT_SERVER_ID       54
#define OPT_END             255

#define DHCP_LEASE_TIME_S   (24u * 3600u)

// ---- Wire format -----------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint8_t  ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint8_t  magic[4];
    uint8_t  options[308];
} dhcp_msg_t;

// ---- Option helpers --------------------------------------------------------

static int opt_u8(uint8_t *p, uint8_t code, uint8_t v)
    { p[0]=code; p[1]=1; p[2]=v; return 3; }

static int opt_u32(uint8_t *p, uint8_t code, uint32_t v) {
    p[0]=code; p[1]=4;
    p[2]=(v>>24)&0xff; p[3]=(v>>16)&0xff; p[4]=(v>>8)&0xff; p[5]=v&0xff;
    return 6;
}

static int opt_read_u8(const uint8_t *o, uint16_t len, uint8_t code) {
    for (uint16_t i=0; i+2<len; ) {
        if (o[i]==OPT_END) break;
        uint8_t l=o[i+1];
        if (o[i]==code && l>=1) return o[i+2];
        i+=2+l;
    }
    return -1;
}

static uint32_t opt_read_u32(const uint8_t *o, uint16_t len, uint8_t code) {
    for (uint16_t i=0; i+5<len; ) {
        if (o[i]==OPT_END) break;
        uint8_t l=o[i+1];
        if (o[i]==code && l==4)
            return ((uint32_t)o[i+2]<<24)|((uint32_t)o[i+3]<<16)|
                   ((uint32_t)o[i+4]<<8)|(uint32_t)o[i+5];
        i+=2+l;
    }
    return 0;
}

// ---- Lease management ------------------------------------------------------

static int find_or_alloc(dhcp_server_t *d, const uint8_t *mac) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    // Reuse existing lease for this MAC.
    for (int i=0; i<DHCP_MAX_LEASES; i++)
        if (d->leases[i].active && memcmp(d->leases[i].mac, mac, 6)==0)
            return i;
    // Allocate a free or expired slot.
    for (int i=0; i<DHCP_MAX_LEASES; i++) {
        if (!d->leases[i].active ||
            (int32_t)(now - d->leases[i].expiry_ms) >= 0) {
            memcpy(d->leases[i].mac, mac, 6);
            d->leases[i].active    = true;
            d->leases[i].expiry_ms = now + DHCP_LEASE_TIME_S * 1000u;
            return i;
        }
    }
    return -1;
}

// Offered IP: gw + 1 + idx  (e.g. gw=10.0.0.1 → .2, .3 …)
static uint32_t lease_ip(dhcp_server_t *d, int idx) {
    return ntohl(ip4_addr_get_u32(ip_2_ip4(&d->gw))) + 1 + (uint32_t)idx;
}

// ---- Send reply ------------------------------------------------------------

static void send_reply(dhcp_server_t *d, struct udp_pcb *pcb,
                       const dhcp_msg_t *req, uint8_t msg_type, int lease_idx) {
    dhcp_msg_t rep;
    memset(&rep, 0, sizeof rep);

    rep.op    = DHCP_OP_REPLY;
    rep.htype = DHCP_HTYPE_ETH;
    rep.hlen  = DHCP_HLEN_ETH;
    rep.xid   = req->xid;

    if (lease_idx >= 0) {
        uint32_t ip_n = htonl(lease_ip(d, lease_idx));
        memcpy(rep.yiaddr, &ip_n, 4);
    }
    uint32_t gw_n = ip4_addr_get_u32(ip_2_ip4(&d->gw));
    memcpy(rep.siaddr, &gw_n, 4);
    memcpy(rep.chaddr, req->chaddr, 16);

    rep.magic[0]=99; rep.magic[1]=130; rep.magic[2]=83; rep.magic[3]=99;

    uint8_t *o = rep.options;
    int n = 0;
    uint32_t gw_he = ntohl(gw_n);
    n += opt_u8 (o+n, OPT_MSG_TYPE,    msg_type);
    n += opt_u32(o+n, OPT_SERVER_ID,   gw_he);
    n += opt_u32(o+n, OPT_LEASE_TIME,  DHCP_LEASE_TIME_S);
    n += opt_u32(o+n, OPT_SUBNET_MASK, ntohl(ip4_addr_get_u32(ip_2_ip4(&d->mask))));
    n += opt_u32(o+n, OPT_ROUTER,      gw_he);
    n += opt_u32(o+n, OPT_DNS,         gw_he);
    o[n++] = OPT_END;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof rep, PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, &rep, sizeof rep);

    // Must use udp_sendto_if() — in a multi-netif setup udp_sendto() may
    // route the broadcast to the wrong interface (e.g. WiFi instead of USB).
    ip_addr_t bcast;
    IP4_ADDR(ip_2_ip4(&bcast), 255, 255, 255, 255);
    IP_SET_TYPE_VAL(bcast, IPADDR_TYPE_V4);
    udp_sendto_if(pcb, p, &bcast, DHCP_PORT_CLIENT, d->netif);
    pbuf_free(p);
}

// ---- UDP receive callback --------------------------------------------------

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
    (void)addr; (void)port;
    dhcp_server_t *d = (dhcp_server_t *)arg;

    if (p->tot_len < 240) { pbuf_free(p); return; }

    dhcp_msg_t msg;
    uint16_t   copied = pbuf_copy_partial(p, &msg, sizeof msg, 0);
    pbuf_free(p);
    if (copied < 240 || msg.op != DHCP_OP_REQUEST) return;

    uint16_t opts_len = (uint16_t)(copied > sizeof msg ? sizeof msg : copied);
    opts_len -= (uint16_t)offsetof(dhcp_msg_t, options);

    int type = opt_read_u8(msg.options, opts_len, OPT_MSG_TYPE);
    const uint8_t *mac = msg.chaddr;

    if (type == DHCPDISCOVER) {
        int idx = find_or_alloc(d, mac);
        if (idx < 0) { printf("DHCP: table full\n"); return; }
        send_reply(d, pcb, &msg, DHCPOFFER, idx);
        uint32_t ip = lease_ip(d, idx);
        printf("DHCP OFFER  %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x\n",
               (ip>>24)&0xff,(ip>>16)&0xff,(ip>>8)&0xff,ip&0xff,
               mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    } else if (type == DHCPREQUEST) {
        int idx = find_or_alloc(d, mac);
        if (idx < 0) { send_reply(d, pcb, &msg, DHCPNAK, -1); return; }
        uint32_t req_ip_he = opt_read_u32(msg.options, opts_len, OPT_REQUESTED_IP);
        if (req_ip_he != 0 && req_ip_he != lease_ip(d, idx)) {
            // Client asking for a different IP — just give ours
        }
        d->leases[idx].expiry_ms =
            to_ms_since_boot(get_absolute_time()) + DHCP_LEASE_TIME_S * 1000u;
        send_reply(d, pcb, &msg, DHCPACK, idx);
        uint32_t ip = lease_ip(d, idx);
        printf("DHCP ACK    %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x\n",
               (ip>>24)&0xff,(ip>>16)&0xff,(ip>>8)&0xff,ip&0xff,
               mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }
}

// ---- Public API ------------------------------------------------------------

void dhcp_server_init(dhcp_server_t *d, ip_addr_t *gw, ip_addr_t *mask,
                      struct netif *netif) {
    ip_addr_copy(d->gw,   *gw);
    ip_addr_copy(d->mask, *mask);
    d->netif = netif;
    memset(d->leases, 0, sizeof d->leases);

    d->udp = udp_new();
    if (!d->udp) { printf("DHCP: udp_new failed\n"); return; }

    udp_recv(d->udp, dhcp_recv, d);
    udp_bind(d->udp, IP_ADDR_ANY, DHCP_PORT_SERVER);

    // Bind to the specific interface so we only receive/send on it.
    // Without this, responses in a multi-netif system go out the wrong IF.
    if (netif) udp_bind_netif(d->udp, netif);

    printf("DHCP server on %s: %s/24\n",
           netif ? netif->name : "?",
           ip4addr_ntoa(ip_2_ip4(gw)));
}

void dhcp_server_deinit(dhcp_server_t *d) {
    if (d->udp) { udp_remove(d->udp); d->udp = NULL; }
}
