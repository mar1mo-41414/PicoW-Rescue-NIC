/*
 * dhcpserver.c — fixed-IP DHCP server
 *
 * Simplifications vs. a full DHCP server:
 *  - No lease table: always offers the same configured IP to any client.
 *  - udp_bind_netif(): restricts the socket to one interface so broadcast
 *    replies go out the correct netif (critical in USB + WiFi dual-netif setup).
 *  - udp_sendto_if(): forces response out the bound interface, not the default.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#include "dhcpserver.h"

#define DHCP_PORT_SERVER    67u
#define DHCP_PORT_CLIENT    68u

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
#define OPT_LEASE_TIME      51
#define OPT_MSG_TYPE        53
#define OPT_SERVER_ID       54
#define OPT_END             255

#define LEASE_TIME_S        (24u * 3600u)

typedef struct __attribute__((packed)) {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint8_t  ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    uint8_t  chaddr[16], sname[64], file[128];
    uint8_t  magic[4];
    uint8_t  options[308];
} dhcp_msg_t;

static int wo_u8(uint8_t *p, uint8_t c, uint8_t v)
    { p[0]=c; p[1]=1; p[2]=v; return 3; }
static int wo_u32(uint8_t *p, uint8_t c, uint32_t v) {
    p[0]=c; p[1]=4;
    p[2]=(v>>24)&0xff; p[3]=(v>>16)&0xff; p[4]=(v>>8)&0xff; p[5]=v&0xff;
    return 6; }
static int ro_u8(const uint8_t *o, uint16_t len, uint8_t c) {
    for (uint16_t i=0; i+2<len;) {
        if (o[i]==OPT_END) break;
        uint8_t l=o[i+1]; if (o[i]==c && l) return o[i+2]; i+=2+l; }
    return -1; }

static void send_reply(dhcp_server_t *d, struct udp_pcb *pcb,
                       const dhcp_msg_t *req, uint8_t type) {
    dhcp_msg_t rep;
    memset(&rep, 0, sizeof rep);
    rep.op=DHCP_OP_REPLY; rep.htype=DHCP_HTYPE_ETH; rep.hlen=DHCP_HLEN_ETH;
    rep.xid=req->xid;

    if (type != DHCPNAK) {
        uint32_t ip_n = ip4_addr_get_u32(&d->fixed_ip);
        memcpy(rep.yiaddr, &ip_n, 4);
    }
    uint32_t gw_n = ip4_addr_get_u32(ip_2_ip4(&d->gw));
    memcpy(rep.siaddr, &gw_n, 4);
    memcpy(rep.chaddr, req->chaddr, 16);
    rep.magic[0]=99; rep.magic[1]=130; rep.magic[2]=83; rep.magic[3]=99;

    uint8_t *o = rep.options; int n = 0;
    uint32_t gw_he = ntohl(gw_n);
    uint32_t mask_he = ntohl(ip4_addr_get_u32(ip_2_ip4(&d->mask)));
    n += wo_u8 (o+n, OPT_MSG_TYPE,    type);
    n += wo_u32(o+n, OPT_SERVER_ID,   gw_he);
    n += wo_u32(o+n, OPT_LEASE_TIME,  LEASE_TIME_S);
    n += wo_u32(o+n, OPT_SUBNET_MASK, mask_he);
    n += wo_u32(o+n, OPT_ROUTER,      gw_he);
    n += wo_u32(o+n, OPT_DNS,         gw_he);
    o[n++] = OPT_END;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof rep, PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, &rep, sizeof rep);

    ip_addr_t bcast;
    IP_ADDR4(&bcast, 255, 255, 255, 255);
    udp_sendto_if(pcb, p, &bcast, DHCP_PORT_CLIENT, d->netif);
    pbuf_free(p);
}

static void dhcp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr, u16_t port) {
    (void)addr; (void)port;
    dhcp_server_t *d = (dhcp_server_t *)arg;
    if (!p) return;

    dhcp_msg_t msg;
    uint16_t copied = pbuf_copy_partial(p, &msg, sizeof msg, 0);
    pbuf_free(p);
    if (copied < 240 || msg.op != DHCP_OP_REQUEST) return;

    uint16_t olen = (uint16_t)(copied - (uint16_t)offsetof(dhcp_msg_t, options));
    int type = ro_u8(msg.options, olen, OPT_MSG_TYPE);

    const uint8_t *mac = msg.chaddr;
    char ip_str[16];
    snprintf(ip_str, sizeof ip_str, "%u.%u.%u.%u",
             ip4_addr1(&d->fixed_ip), ip4_addr2(&d->fixed_ip),
             ip4_addr3(&d->fixed_ip), ip4_addr4(&d->fixed_ip));

    if (type == DHCPDISCOVER) {
        send_reply(d, pcb, &msg, DHCPOFFER);
        printf("DHCP OFFER  %s -> %02x:%02x:%02x:%02x:%02x:%02x\n",
               ip_str, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    } else if (type == DHCPREQUEST) {
        send_reply(d, pcb, &msg, DHCPACK);
        printf("DHCP ACK    %s -> %02x:%02x:%02x:%02x:%02x:%02x\n",
               ip_str, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }
}

void dhcp_server_init(dhcp_server_t *d,
                      ip_addr_t *gw, ip_addr_t *mask,
                      const ip4_addr_t *fixed_ip,
                      struct netif *netif) {
    ip_addr_copy(d->gw, *gw);
    ip_addr_copy(d->mask, *mask);
    ip4_addr_copy(d->fixed_ip, *fixed_ip);
    d->netif = netif;

    d->udp = udp_new();
    if (!d->udp) { printf("DHCP: udp_new failed\n"); return; }
    udp_recv(d->udp, dhcp_recv, d);
    udp_bind(d->udp, IP_ADDR_ANY, DHCP_PORT_SERVER);
    udp_bind_netif(d->udp, netif);   // restrict to this interface only

    printf("DHCP ready on %s: fixed IP = %s\n",
           netif->name, ip4addr_ntoa(fixed_ip));
}

void dhcp_server_deinit(dhcp_server_t *d) {
    if (d->udp) { udp_remove(d->udp); d->udp = NULL; }
}
