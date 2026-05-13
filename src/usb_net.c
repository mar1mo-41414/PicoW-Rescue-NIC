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
// lwIP TX path: lwIP → TinyUSB → USB host
// ---------------------------------------------------------------------------
static err_t usb_netif_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (!tud_ready()) return ERR_USE;
    // Do NOT call tud_task() here — we may already be inside tud_task()
    // (called via tud_network_recv_cb → DHCP → udp_sendto_if → this function).
    // Calling tud_task() recursively corrupts TinyUSB's internal state.
    if (!tud_network_can_xmit(p->tot_len)) return ERR_BUF;
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

    printf("USB net: Pico=10.0.0.1  Client=10.0.0.2 (DHCP fixed)\n");
}

struct netif *usb_net_get_netif(void) { return &usb_netif; }

// ---------------------------------------------------------------------------
// TinyUSB callbacks
// ---------------------------------------------------------------------------

// Called by TinyUSB when the USB host sends an Ethernet frame.
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
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
void tud_network_init_cb(void) {
    // Link is already marked up in usb_netif_init via NETIF_FLAG_LINK_UP.
    printf("USB NCM: host activated data interface\n");
}
