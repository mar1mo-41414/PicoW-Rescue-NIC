/*
 * nat.c — Minimal NAPT for PicoW-NIC
 *
 * pico-sdk's bundled lwIP snapshot does not include ip4_napt.c, so we
 * implement a simple NAT table here.
 *
 * Strategy:
 *   • Hook into lwIP IP input via LWIP_HOOK_IP4_INPUT (defined in lwipopts.h).
 *   • When a packet arrives on the USB interface (10.0.0.x) destined for a
 *     WiFi client (192.168.4.x), lwIP's IP_FORWARD will route it.
 *   • We rewrite the source IP to the WiFi AP gateway (192.168.4.1) and
 *     record the mapping in the NAT table.
 *   • When the reply arrives on WiFi (src=192.168.4.x, dst=192.168.4.1 +
 *     mapped port), we rewrite dst back to the original USB client.
 *
 * Supported: TCP, UDP, ICMP echo.
 * Checksum update: incremental (RFC 1624).
 *
 * Note: lwIP's IP_FORWARD handles the actual packet forwarding between
 * netifs.  We only patch the IP/transport headers before that happens.
 */

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "lwip/ip4.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip.h"     // IP_PROTO_TCP/UDP/ICMP
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/icmp.h"

#include "nat.h"
#include "usb_net.h"
#include "wifi_ap.h"

// ---------------------------------------------------------------------------
// NAT table entry
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t  orig_src;      // original source IP (USB client, host-order)
    uint32_t  orig_dst;      // original destination IP (WiFi client)
    uint16_t  orig_sport;    // original source port
    uint16_t  orig_dport;    // original dest port
    uint16_t  nat_port;      // port we assigned (mapped src port)
    uint8_t   proto;         // IP_PROTO_TCP / IP_PROTO_UDP / IPPROTO_ICMP
    bool      active;
    uint32_t  last_seen_ms;
} nat_entry_t;

static nat_entry_t nat_table[NAT_TABLE_SIZE];
static uint16_t    next_port = 49152u;   // ephemeral port range start

// WiFi AP IP (masquerade address) — filled in nat_init()
static uint32_t wifi_ip_n = 0;    // network-byte-order
static uint32_t usb_net_n = 0;    // 10.0.0.0 network, network-byte-order
static uint32_t usb_mask_n = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static uint16_t alloc_port(void) {
    uint16_t p = next_port++;
    if (next_port == 0 || next_port > 65535u) next_port = 49152u;
    return p;
}

// Incremental checksum update per RFC 1624 s4
// Returns updated checksum (ones-complement) after changing word from ~old to new.
static uint16_t chksum_adjust(uint16_t chksum, uint16_t old_val, uint16_t new_val) {
    uint32_t c = (~chksum & 0xffffu) + (~old_val & 0xffffu) + new_val;
    while (c >> 16) c = (c & 0xffffu) + (c >> 16);
    return (uint16_t)(~c & 0xffffu);
}

// Adjust checksum for a 32-bit word change (e.g. IP address rewrite)
static uint16_t chksum_adjust32(uint16_t chksum, uint32_t old32, uint32_t new32) {
    chksum = chksum_adjust(chksum, (uint16_t)(old32 >> 16), (uint16_t)(new32 >> 16));
    chksum = chksum_adjust(chksum, (uint16_t)(old32 & 0xffff), (uint16_t)(new32 & 0xffff));
    return chksum;
}

// ---------------------------------------------------------------------------
// Table lookup / allocation
// ---------------------------------------------------------------------------

static nat_entry_t *find_outbound(uint32_t src, uint32_t dst,
                                   uint16_t sport, uint16_t dport, uint8_t proto) {
    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        nat_entry_t *e = &nat_table[i];
        if (e->active && e->proto == proto &&
            e->orig_src == src && e->orig_dst == dst &&
            e->orig_sport == sport && e->orig_dport == dport)
            return e;
    }
    return NULL;
}

static nat_entry_t *find_inbound(uint32_t dst_orig, uint16_t nat_port, uint8_t proto) {
    // dst_orig = WiFi client's source IP (= original destination)
    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        nat_entry_t *e = &nat_table[i];
        if (e->active && e->proto == proto &&
            e->nat_port == nat_port && e->orig_dst == dst_orig)
            return e;
    }
    return NULL;
}

static nat_entry_t *alloc_entry(void) {
    uint32_t now = now_ms();
    for (int i = 0; i < NAT_TABLE_SIZE; i++) {
        nat_entry_t *e = &nat_table[i];
        if (!e->active) return e;
        uint32_t ttl = (e->proto == IP_PROTO_TCP) ? NAT_TCP_TTL_MS :
                       (e->proto == IP_PROTO_UDP) ? NAT_UDP_TTL_MS :
                                                    NAT_ICMP_TTL_MS;
        if ((int32_t)(now - e->last_seen_ms) > (int32_t)ttl) {
            e->active = false;
            return e;
        }
    }
    return NULL;   // table full
}

// ---------------------------------------------------------------------------
// Outbound NAT (USB → WiFi): rewrite src IP + port
// Packet header is in contiguous memory after pbuf_header().
// ---------------------------------------------------------------------------

static int nat_outbound(struct pbuf *p) {
    // We need the full IP header to be in the first pbuf.
    struct ip_hdr *iph = (struct ip_hdr *)p->payload;
    uint16_t ihl = IPH_HL(iph) * 4u;
    if (p->len < ihl + 4) return 0;   // too short

    uint8_t  proto    = IPH_PROTO(iph);
    uint32_t orig_src = iph->src.addr;   // network-byte-order
    uint32_t orig_dst = iph->dest.addr;

    uint8_t *tp = (uint8_t *)p->payload + ihl;
    uint16_t orig_sport = 0, orig_dport = 0;

    if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) {
        orig_sport = ((uint16_t)tp[0] << 8) | tp[1];
        orig_dport = ((uint16_t)tp[2] << 8) | tp[3];
    } else if (proto == IP_PROTO_ICMP) {
        // Only NAT ICMP echo requests
        if (tp[0] != ICMP_ECHO) return 0;
        // Use ICMP id as "port"
        orig_sport = ((uint16_t)tp[4] << 8) | tp[5];
        orig_dport = 0;
    } else {
        return 0;
    }

    // Look up existing entry
    nat_entry_t *e = find_outbound(ntohl(orig_src), ntohl(orig_dst),
                                    orig_sport, orig_dport, proto);
    if (!e) {
        e = alloc_entry();
        if (!e) {
            printf("NAT: table full, dropping outbound\n");
            return 0;   // let packet through without NAT (will fail routing)
        }
        e->orig_src   = ntohl(orig_src);
        e->orig_dst   = ntohl(orig_dst);
        e->orig_sport = orig_sport;
        e->orig_dport = orig_dport;
        e->nat_port   = alloc_port();
        e->proto      = proto;
        e->active     = true;
    }
    e->last_seen_ms = now_ms();

    // --- Rewrite IP source address ---
    uint16_t old_ip_chk = iph->_chksum;
    iph->src.addr = wifi_ip_n;
    iph->_chksum  = chksum_adjust32(old_ip_chk, orig_src, wifi_ip_n);

    // --- Rewrite transport source port ---
    if (proto == IP_PROTO_TCP) {
        struct tcp_hdr *tcph = (struct tcp_hdr *)tp;
        uint16_t old_chk = tcph->chksum;
        uint16_t new_sport = htons(e->nat_port);
        tcph->src  = new_sport;
        // TCP checksum covers pseudo-header (IP addrs) + TCP segment
        old_chk = chksum_adjust32(old_chk, orig_src, wifi_ip_n);
        old_chk = chksum_adjust(old_chk, htons(orig_sport), new_sport);
        tcph->chksum = old_chk;
    } else if (proto == IP_PROTO_UDP) {
        uint16_t *uph = (uint16_t *)tp;
        uint16_t new_sport = htons(e->nat_port);
        uint16_t old_chk   = uph[3];
        uph[0] = new_sport;
        if (old_chk != 0) {   // UDP checksum is optional; 0 = disabled
            old_chk = chksum_adjust32(old_chk, orig_src, wifi_ip_n);
            old_chk = chksum_adjust(old_chk, htons(orig_sport), new_sport);
            uph[3]  = old_chk ? old_chk : 0xffffu;
        }
    } else {
        // ICMP: rewrite echo id in payload, update ICMP checksum
        uint16_t new_id   = htons(e->nat_port);
        uint16_t old_id   = ((uint16_t)tp[4] << 8) | tp[5];
        uint16_t old_ichk = ((uint16_t)tp[2] << 8) | tp[3];
        tp[4] = (uint8_t)(new_id >> 8);
        tp[5] = (uint8_t)(new_id & 0xff);
        uint16_t new_ichk = chksum_adjust(old_ichk, htons(old_id), new_id);
        tp[2] = (uint8_t)(new_ichk >> 8);
        tp[3] = (uint8_t)(new_ichk & 0xff);
    }

    return 0;   // 0 = let lwIP continue forwarding (IP_FORWARD will send it)
}

// ---------------------------------------------------------------------------
// Inbound NAT (WiFi → USB): restore original dst IP + port
// ---------------------------------------------------------------------------

static int nat_inbound(struct pbuf *p) {
    struct ip_hdr *iph = (struct ip_hdr *)p->payload;
    uint16_t ihl = IPH_HL(iph) * 4u;
    if (p->len < ihl + 4) return 0;

    uint8_t  proto    = IPH_PROTO(iph);
    uint32_t pkt_src  = iph->src.addr;   // WiFi client
    uint32_t pkt_dst  = iph->dest.addr;  // must equal wifi_ip_n

    if (pkt_dst != wifi_ip_n) return 0;

    uint8_t *tp = (uint8_t *)p->payload + ihl;
    uint16_t dport = 0;
    uint32_t orig_dst_he = ntohl(pkt_src);   // WiFi client = original dst

    if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) {
        dport = ((uint16_t)tp[2] << 8) | tp[3];
    } else if (proto == IP_PROTO_ICMP) {
        // ICMP echo reply
        if (tp[0] != ICMP_ER) return 0;
        dport = ((uint16_t)tp[4] << 8) | tp[5];
    } else {
        return 0;
    }

    nat_entry_t *e = find_inbound(orig_dst_he, ntohs(dport), proto);
    if (!e) return 0;   // no mapping → pass to normal input

    e->last_seen_ms = now_ms();

    uint32_t new_dst_n = htonl(e->orig_src);
    // Rewrite IP destination
    uint16_t old_ip_chk = iph->_chksum;
    iph->dest.addr  = new_dst_n;
    iph->_chksum    = chksum_adjust32(old_ip_chk, pkt_dst, new_dst_n);

    if (proto == IP_PROTO_TCP) {
        struct tcp_hdr *tcph = (struct tcp_hdr *)tp;
        uint16_t old_chk  = tcph->chksum;
        uint16_t new_dport = htons(e->orig_sport);
        tcph->dest   = new_dport;
        old_chk = chksum_adjust32(old_chk, pkt_dst, new_dst_n);
        old_chk = chksum_adjust(old_chk, dport, new_dport);
        tcph->chksum = old_chk;
    } else if (proto == IP_PROTO_UDP) {
        uint16_t *uph  = (uint16_t *)tp;
        uint16_t new_dport = htons(e->orig_sport);
        uint16_t old_chk   = uph[3];
        uph[1] = new_dport;
        if (old_chk != 0) {
            old_chk = chksum_adjust32(old_chk, pkt_dst, new_dst_n);
            old_chk = chksum_adjust(old_chk, dport, new_dport);
            uph[3]  = old_chk ? old_chk : 0xffffu;
        }
    } else {
        // ICMP echo reply: restore id
        uint16_t new_id   = htons(e->orig_sport);
        uint16_t old_id   = ((uint16_t)tp[4] << 8) | tp[5];
        uint16_t old_ichk = ((uint16_t)tp[2] << 8) | tp[3];
        tp[4] = (uint8_t)(new_id >> 8);
        tp[5] = (uint8_t)(new_id & 0xff);
        uint16_t new_ichk = chksum_adjust(old_ichk, htons(old_id), new_id);
        tp[2] = (uint8_t)(new_ichk >> 8);
        tp[3] = (uint8_t)(new_ichk & 0xff);
    }

    // Now the packet is destined for 10.0.0.x — let lwIP forward it.
    return 0;
}

// ---------------------------------------------------------------------------
// LWIP_HOOK_IP4_INPUT entry point
// Called by lwIP for every incoming IPv4 packet before normal processing.
// Return 0 to let lwIP continue; return 1 to claim the packet.
// ---------------------------------------------------------------------------
int nat_ip4_input_hook(struct pbuf *p, struct netif *inp) {
    if (!wifi_ip_n || !usb_net_n) return 0;

    struct ip_hdr *iph = (struct ip_hdr *)p->payload;
    if (p->len < IP_HLEN) return 0;

    uint32_t src = iph->src.addr;
    uint32_t dst = iph->dest.addr;

    struct netif *usb_if  = usb_net_get_netif();
    struct netif *wifi_if = wifi_ap_get_netif();
    if (!usb_if || !wifi_if) return 0;

    // Outbound: packet arriving on USB, destined for WiFi subnet
    if (inp == usb_if) {
        // Is the destination on the WiFi subnet?
        uint32_t wifi_net  = wifi_if->ip_addr.addr & wifi_if->netmask.addr;
        uint32_t dst_net   = dst & wifi_if->netmask.addr;
        if (dst_net == wifi_net && dst != wifi_ip_n) {
            nat_outbound(p);
        }
        return 0;
    }

    // Inbound: packet arriving on WiFi, destined for our masquerade IP
    if (inp == wifi_if && dst == wifi_ip_n) {
        nat_inbound(p);
        return 0;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------
void nat_init(void) {
    memset(nat_table, 0, sizeof(nat_table));

    struct netif *wifi_if = wifi_ap_get_netif();
    if (wifi_if) {
        wifi_ip_n  = wifi_if->ip_addr.addr;   // 192.168.4.1 in network order
    }

    struct netif *usb_if = usb_net_get_netif();
    if (usb_if) {
        usb_net_n  = usb_if->ip_addr.addr & usb_if->netmask.addr;
        usb_mask_n = usb_if->netmask.addr;
    }

    printf("NAT: init, masquerade IP=%s table=%d entries\n",
           ip4addr_ntoa((ip4_addr_t *)&wifi_ip_n), NAT_TABLE_SIZE);
}
