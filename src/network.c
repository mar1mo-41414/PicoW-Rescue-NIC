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
 */

#include <stdio.h>
#include "pico/cyw43_arch.h"
#include "cyw43.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "usb_net.h"
#include "wifi_ap.h"
#include "nat.h"
#include "network.h"

void network_init(void) {
    printf("Network: IP forwarding enabled (IP_FORWARD=1)\n");
    nat_init();
    printf("Network: NAPT active (USB→WiFi masquerade via nat.c)\n");
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
        int rssi = 0;
        cyw43_wifi_get_rssi(&cyw43_state, &rssi);
        printf("WiFi (%s%c): IP=%-16s link=%s  RSSI=%d dBm\n",
               wifi->name,
               (char)('0' + wifi->num),
               ip4addr_ntoa(netif_ip4_addr(wifi)),
               netif_is_link_up(wifi) ? "UP" : "DOWN",
               rssi);
    }
}
