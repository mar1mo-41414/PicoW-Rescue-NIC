/*
 * usb_net.c — TinyUSB CDC-NCM ↔ lwIP bridge
 *
 * USB subnet: 10.0.0.0/24
 *   Pico   : 10.0.0.1 (USB netif)
 *   PC     : 10.0.0.2 (DHCP fixed)
 *
 * Important design notes:
 *
 * 1. usb_netif_linkoutput must NOT call tud_task() internally.
 *    tud_task() → tud_network_recv_cb() → DHCP handler → linkoutput → tud_task()
 *    would be a recursive call and corrupts TinyUSB state.
 *    Solution: drop the packet (return ERR_BUF) if the NCM transmitter is busy.
 *    For DHCP/TCP, the client retransmits; single-packet drops are fine.
 *
 * 2. Link is brought up immediately at init (not waiting for tud_network_init_cb)
 *    so lwIP can process packets as soon as the USB host sends them.
 */

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/icmp.h"
#include "netif/ethernet.h"

#include "dhcpserver.h"
#include "usb_net.h"

// Pico-side MAC for the USB netif.
// Must be non-const (TinyUSB net_device.h declares: extern uint8_t[6]).
// Must match string index 6 in usb_descriptors.c ("020284696000").
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x69, 0x60, 0x00};

static struct netif  usb_netif;
static dhcp_server_t usb_dhcp;

// ---------------------------------------------------------------------------
// Software checksum finalizer
//
// lwIP's ip4_forward() zeros IP/ICMP/TCP/UDP checksums expecting the NIC
// hardware to fill them in (checksum offload).  Our USB NCM driver is pure
// software, so we must compute them here before handing off to TinyUSB.
//
// Called for every frame that goes out the USB interface — also handles
// Pico-originated packets (DHCP, ARP replies) where ip4_output already set
// the checksum; recomputing an already-correct checksum is still correct.
// ---------------------------------------------------------------------------
static void usb_fill_checksums(struct pbuf *p) {
    // Need at least Ethernet(14) + IP header(20)
    if (p->tot_len < 14 + 20) return;

    // Work only with contiguous pbufs (LWIP_NETIF_TX_SINGLE_PBUF=1 guarantees this).
    if (p->len < p->tot_len) return;   // chained — skip (shouldn't happen)

    uint8_t *frame = (uint8_t *)p->payload;

    // Only IPv4
    if (frame[12] != 0x08 || frame[13] != 0x00) return;

    struct ip_hdr *iph = (struct ip_hdr *)(frame + 14);
    uint16_t ihl      = IPH_HL(iph) * 4u;
    uint16_t tot      = lwip_ntohs(IPH_LEN(iph));

    if (p->tot_len < (uint16_t)(14 + tot)) return;
    if (tot < ihl) return;

    // --- IP checksum ---
    IPH_CHKSUM_SET(iph, 0);
    IPH_CHKSUM_SET(iph, inet_chksum(iph, ihl));

    // --- Transport checksum ---
    uint8_t  proto       = IPH_PROTO(iph);
    uint8_t *tp          = frame + 14 + ihl;
    uint16_t payload_len = (uint16_t)(tot - ihl);

    if (proto == IP_PROTO_ICMP) {
        if (payload_len < (uint16_t)sizeof(struct icmp_hdr)) return;
        struct icmp_hdr *icmph = (struct icmp_hdr *)tp;
        icmph->chksum = 0;
        icmph->chksum = inet_chksum(icmph, payload_len);

    } else if (proto == IP_PROTO_TCP) {
        if (payload_len < TCP_HLEN) return;
        // Copy packed IP address fields to aligned locals for inet_chksum_pseudo.
        ip4_addr_t src4, dst4;
        src4.addr = iph->src.addr; dst4.addr = iph->dest.addr;
        ip_addr_t src_a, dst_a;
        ip_addr_copy_from_ip4(src_a, src4); ip_addr_copy_from_ip4(dst_a, dst4);
        struct pbuf tmp;
        tmp.next = NULL; tmp.payload = tp;
        tmp.len  = tmp.tot_len = payload_len;
        tmp.type_internal = PBUF_ROM; tmp.ref = 1; tmp.flags = 0;
        ((struct tcp_hdr *)tp)->chksum = 0;
        ((struct tcp_hdr *)tp)->chksum = inet_chksum_pseudo(
            &tmp, IP_PROTO_TCP, payload_len,
            ip_2_ip4(&src_a), ip_2_ip4(&dst_a));

    } else if (proto == IP_PROTO_UDP) {
        if (payload_len < UDP_HLEN) return;
        struct udp_hdr *udph = (struct udp_hdr *)tp;
        if (udph->chksum != 0) {   // 0 = checksum disabled (RFC 768)
            ip4_addr_t src4, dst4;
            src4.addr = iph->src.addr; dst4.addr = iph->dest.addr;
            ip_addr_t src_a, dst_a;
            ip_addr_copy_from_ip4(src_a, src4); ip_addr_copy_from_ip4(dst_a, dst4);
            struct pbuf tmp;
            tmp.next = NULL; tmp.payload = tp;
            tmp.len  = tmp.tot_len = payload_len;
            tmp.type_internal = PBUF_ROM; tmp.ref = 1; tmp.flags = 0;
            udph->chksum = 0;
            udph->chksum = inet_chksum_pseudo(
                &tmp, IP_PROTO_UDP, payload_len,
                ip_2_ip4(&src_a), ip_2_ip4(&dst_a));
            if (udph->chksum == 0) udph->chksum = 0xffffU; // ones' complement 0
        }
    }
}

// ---------------------------------------------------------------------------
// lwIP TX path: lwIP → TinyUSB → USB host
// ---------------------------------------------------------------------------
static err_t usb_netif_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;
#if VERBOSE_LOG
    bool ready = tud_ready();
    bool can   = tud_network_can_xmit(p->tot_len);
    printf("USB TX: %u bytes  ready=%d can_xmit=%d\n",
           (unsigned)p->tot_len, ready, can);
    if (!ready) return ERR_USE;
    if (!can)   return ERR_BUF;
#else
    if (!tud_ready())                    return ERR_USE;
    if (!tud_network_can_xmit(p->tot_len)) return ERR_BUF;
#endif
    usb_fill_checksums(p);   // fix checksums zeroed by ip4_forward (HW offload path)
    tud_network_xmit(p, 0);
    return ERR_OK;
}

// Called by TinyUSB to copy our pending frame into the USB NCM transfer buffer.
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    (void)arg;
    return pbuf_copy_partial((struct pbuf *)ref, dst,
                             ((struct pbuf *)ref)->tot_len, 0);
}

static err_t usb_netif_init(struct netif *netif) {
    netif->linkoutput = usb_netif_linkoutput;
    netif->output     = etharp_output;
    netif->mtu        = 1500;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                        NETIF_FLAG_ETHERNET   | NETIF_FLAG_LINK_UP;
    SMEMCPY(netif->hwaddr, tud_network_mac_address, ETH_HWADDR_LEN);
    netif->name[0]    = 'u'; netif->name[1] = 's';
    return ERR_OK;
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------
void usb_net_init(void) {
    ip4_addr_t ip, mask, gw, client_ip;
    IP4_ADDR(&ip,        10, 0, 0, 1);
    IP4_ADDR(&mask,      255, 255, 255, 0);
    IP4_ADDR(&gw,        10, 0, 0, 1);
    IP4_ADDR(&client_ip, 10, 0, 0, 2);   // fixed IP handed to USB client

    ip_addr_t gw_ia, mask_ia;
    ip_addr_copy_from_ip4(gw_ia, gw);
    ip_addr_copy_from_ip4(mask_ia, mask);

    netif_add(&usb_netif, &ip, &mask, &gw,
              NULL, usb_netif_init, ethernet_input);
    netif_set_up(&usb_netif);

    // Set USB as default netif: used by lwIP for outbound routing when no
    // explicit route exists (e.g., DHCP broadcast responses).
    netif_set_default(&usb_netif);

    // DHCP server bound to usb_netif; always assigns 10.0.0.2.
    dhcp_server_init(&usb_dhcp, &gw_ia, &mask_ia, &client_ip, &usb_netif);

    // Push DHCP option 121 (Classless Static Route, RFC 3442) so the USB
    // client automatically routes 192.168.4.0/24 via 10.0.0.1 without any
    // manual "ip route add" command.
    IP4_ADDR(&usb_dhcp.route_net, 192, 168, 4, 0);
    usb_dhcp.route_prefix_len = 24;
    IP4_ADDR(&usb_dhcp.route_gw, 10, 0, 0, 1);

    printf("USB net: Pico=10.0.0.1  Client=10.0.0.2 (DHCP fixed)\n");
}

struct netif *usb_net_get_netif(void) { return &usb_netif; }

// ---------------------------------------------------------------------------
// TinyUSB callbacks
// ---------------------------------------------------------------------------

// Called by TinyUSB when the USB host sends an Ethernet frame.
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
#if VERBOSE_LOG
    uint16_t etype = (size >= 14) ? (uint16_t)((src[12] << 8) | src[13]) : 0;
    if (etype == 0x0800 && size >= 14+20+8) {
        uint8_t proto = src[14+9];
        uint16_t dport = (uint16_t)((src[14+20+2] << 8) | src[14+20+3]);
        printf("USB RX: %u bytes  IPv4 proto=%u dport=%u\r\n",
               (unsigned)size, proto, dport);
    } else {
        printf("USB RX: %u bytes  etype=0x%04x\r\n", (unsigned)size, etype);
    }
#endif
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (p) {
        pbuf_take(p, src, size);
        if (usb_netif.input(p, &usb_netif) != ERR_OK)
            pbuf_free(p);
    }
    tud_network_recv_renew();   // re-arm NCM receiver for next frame
    return true;
}

// Called when the USB host activates the NCM data interface (alt=1).
// NOTE: TinyUSB's ncm_device.c does NOT call this — it is only called by
// the ECM/RNDIS driver.  The print below will NOT appear for NCM.
// Link is brought up unconditionally via NETIF_FLAG_LINK_UP at init.
void tud_network_init_cb(void) {
#if VERBOSE_LOG
    printf("USB NCM: tud_network_init_cb (ECM/RNDIS path)\n");
#endif
}
