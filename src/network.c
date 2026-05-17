/*
 * network.c — IP routing and NAPT between USB and WiFi subnets
 *
 * Topology:
 *   USB side  : 10.0.0.0/24   (Pico = 10.0.0.1)
 *   WiFi side : 192.168.4.0/24 (Pico = 192.168.4.1)
 *
 * IP_FORWARD=1 (lwipopts.h) lets lwIP route between netifs automatically.
 * NAPT is provided by src/nat.c via LWIP_HOOK_IP4_INPUT.
 *
 * Effect: USB PC can reach WiFi clients without adding routes.
 * WiFi → USB direction: add a route on the WiFi client if needed.
 *   Linux:   ip route add 10.0.0.0/24 via 192.168.4.1
 *   macOS:   sudo route add 10.0.0.0/24 192.168.4.1
 *   Windows: route add 10.0.0.0 mask 255.255.255.0 192.168.4.1
 *
 * Checksum note:
 *   lwIP's ip4_forward() unconditionally zeroes IP/ICMP/TCP/UDP checksums
 *   when CHECKSUM_GEN_* = 1 (hardware offload design assumption).
 *   CYW43 does not recompute them, so forwarded packets would arrive at the
 *   WiFi client with zero checksums and be dropped.
 *   Fix: wrap the WiFi netif's linkoutput to recompute all checksums in
 *   software before handing the frame to the CYW43 driver, exactly as
 *   usb_fill_checksums() does for the USB direction.
 */

#include <string.h>
#include <stdio.h>
#include "pico/cyw43_arch.h"
#include "cyw43.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/icmp.h"
#include "usb_net.h"
#include "wifi_ap.h"
#include "nat.h"
#include "network.h"

// ---------------------------------------------------------------------------
// WiFi TX software checksum engine
//
// ip4_forward() zeroes IP/transport checksums (hardware offload assumption).
// CYW43 does not fill them in.  This wrapper recomputes them in software
// before the frame goes to the CYW43 driver, mirroring usb_fill_checksums().
// ---------------------------------------------------------------------------
static netif_linkoutput_fn wifi_orig_linkoutput = NULL;

static void wifi_fill_checksums(struct pbuf *p) {
    if (p->tot_len < 14 + 20) return;
    if (p->len < p->tot_len) return;   // chained — skip (LWIP_NETIF_TX_SINGLE_PBUF should prevent this)

    uint8_t *frame = (uint8_t *)p->payload;
    if (frame[12] != 0x08 || frame[13] != 0x00) return;   // IPv4 only

    struct ip_hdr *iph = (struct ip_hdr *)(frame + 14);
    uint16_t ihl = IPH_HL(iph) * 4u;
    uint16_t tot = lwip_ntohs(IPH_LEN(iph));

    if (p->tot_len < (uint16_t)(14 + tot)) return;
    if (tot < ihl) return;

    // --- IP header checksum ---
    IPH_CHKSUM_SET(iph, 0);
    IPH_CHKSUM_SET(iph, inet_chksum(iph, ihl));

    // --- Transport layer checksum ---
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
            if (udph->chksum == 0) udph->chksum = 0xffffU;
        }
    }
}

static err_t wifi_linkoutput_chksum(struct netif *netif, struct pbuf *p) {
    wifi_fill_checksums(p);
    return wifi_orig_linkoutput(netif, p);
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------
void network_init(void) {
    printf("Network: IP forwarding enabled (IP_FORWARD=1)\n");
    nat_init();
    printf("Network: NAPT active (USB→WiFi masquerade via nat.c)\n");

    // Wrap WiFi netif linkoutput to recompute checksums zeroed by ip4_forward.
    // CYW43 does not do IP/ICMP/TCP/UDP checksum offload, so we must do it
    // in software here — same principle as usb_fill_checksums() for USB TX.
    struct netif *wifi = wifi_ap_get_netif();
    if (wifi && wifi->linkoutput) {
        wifi_orig_linkoutput = wifi->linkoutput;
        wifi->linkoutput = wifi_linkoutput_chksum;
        printf("Network: WiFi TX checksum wrapper installed\n");
    } else {
        printf("Network: WARNING — could not install WiFi TX checksum wrapper\n");
    }
}

void network_print_status(void) {
    struct netif *usb  = usb_net_get_netif();
    struct netif *wifi = wifi_ap_get_netif();

    printf("--- Status ---\n");

    if (usb) {
        printf("USB  (%s%c): IP=%-16s link=%s\n",
               usb->name,
               (char)('0' + usb->num),
               ip4addr_ntoa(netif_ip4_addr(usb)),
               netif_is_link_up(usb) ? "UP" : "DOWN");
    }

    if (wifi) {
        int32_t rssi = 0;
        cyw43_wifi_get_rssi(&cyw43_state, &rssi);
        printf("WiFi (%s%c): IP=%-16s link=%s  RSSI=%d dBm\n",
               wifi->name,
               (char)('0' + wifi->num),
               ip4addr_ntoa(netif_ip4_addr(wifi)),
               netif_is_link_up(wifi) ? "UP" : "DOWN",
               rssi);
    }
}
