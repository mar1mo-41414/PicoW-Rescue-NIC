/*
 * usb_net.c — TinyUSB CDC-NCM ↔ lwIP bridge
 *
 * Creates a lwIP Ethernet netif backed by the TinyUSB NCM device class.
 * Runs a DHCP server on 10.0.0.0/24 (Pico = 10.0.0.1, PC = 10.0.0.2+).
 *
 * TinyUSB NCM xmit API (deferred / zero-copy pattern):
 *   tud_network_xmit(ref, arg)        — schedule a packet
 *   tud_network_xmit_cb(dst, ref, arg) — called by TinyUSB to copy data
 *
 * RX path:
 *   tud_network_recv_cb(src, size)    — TinyUSB delivers Ethernet frame
 *   → allocate pbuf, pbuf_take, call netif->input()
 *   → tud_network_recv_renew()       — re-arm the receiver
 */

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"

#include "dhcpserver.h"
#include "usb_net.h"

// ---------------------------------------------------------------------------
// MAC address — Pico (device) side.
// 02:xx = locally administered, unicast.
// Must match the MAC string in usb_descriptors.c index 6 ("020284696000").
// ---------------------------------------------------------------------------
uint8_t const tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x69, 0x60, 0x00};

static struct netif   usb_netif;
static dhcp_server_t  usb_dhcp;

// ---------------------------------------------------------------------------
// lwIP output path  (lwIP → TinyUSB → USB host)
// ---------------------------------------------------------------------------

// Called by lwIP when it wants to send a frame on the USB interface.
// We pass the pbuf pointer as the "ref" to tud_network_xmit.
// TinyUSB will call tud_network_xmit_cb() (below) to copy the payload.
static err_t usb_netif_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;

    // Spin until TinyUSB has room for a new packet.
    // In a cooperative loop this is safe; the loop is short (< 1 frame time).
    for (int retries = 0; retries < 200; retries++) {
        if (!tud_ready())
            return ERR_USE;
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        // Pump TinyUSB to finish transmitting the previous frame.
        tud_task();
    }
    return ERR_TIMEOUT;
}

// Called by TinyUSB to copy our pending frame into the USB transfer buffer.
// `ref` is the pbuf pointer we passed to tud_network_xmit().
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    (void)arg;
    struct pbuf *p = (struct pbuf *)ref;
    if (!p) return 0;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
    // Note: pbuf is still owned by lwIP — do NOT free here.
}

// ---------------------------------------------------------------------------
// lwIP netif init callback
// ---------------------------------------------------------------------------
static err_t usb_netif_init(struct netif *netif) {
    netif->linkoutput = usb_netif_linkoutput;
    netif->output     = etharp_output;
    netif->mtu        = CFG_TUD_NET_MTU;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    netif->flags      = NETIF_FLAG_BROADCAST |
                        NETIF_FLAG_ETHARP    |
                        NETIF_FLAG_ETHERNET;
    SMEMCPY(netif->hwaddr, tud_network_mac_address, ETH_HWADDR_LEN);
    netif->name[0]    = 'u';
    netif->name[1]    = 's';
    return ERR_OK;
}

// ---------------------------------------------------------------------------
// Public init
// ---------------------------------------------------------------------------
void usb_net_init(void) {
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   10, 0, 0, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw,   10, 0, 0, 1);   // Pico is the gateway for the USB subnet

    netif_add(&usb_netif, &ip, &mask, &gw,
              NULL, usb_netif_init, ethernet_input);
    netif_set_up(&usb_netif);
    // Link state is set up when TinyUSB connects (tud_network_init_cb).

    // DHCP server: hands out 10.0.0.2 – 10.0.0.9 to USB clients.
    dhcp_server_init(&usb_dhcp, &gw, &mask);

    printf("USB net: 10.0.0.1/24 ready (DHCP active)\n");
}

struct netif *usb_net_get_netif(void) {
    return &usb_netif;
}

// ---------------------------------------------------------------------------
// TinyUSB RX callback  (USB host → Pico → lwIP)
// ---------------------------------------------------------------------------
void tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (netif_is_up(&usb_netif)) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) {
            pbuf_take(p, src, size);
            if (usb_netif.input(p, &usb_netif) != ERR_OK) {
                pbuf_free(p);
            }
        }
    }
    // Re-arm the NCM receiver for the next frame.
    tud_network_recv_renew();
}

// Called when the USB host activates the NCM data interface.
void tud_network_init_cb(void) {
    netif_set_link_up(&usb_netif);
    printf("USB NCM: host connected, link UP\n");
}
