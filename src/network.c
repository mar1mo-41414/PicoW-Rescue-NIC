/*
 * network.c — IP routing and NAPT between USB and WiFi subnets
 *
 * Topology:
 *   USB side  : 10.0.0.0/24   (Pico = 10.0.0.1)
 *   WiFi side : 192.168.4.0/24 (Pico = 192.168.4.1)
 *
 * NAPT is enabled on the WiFi AP interface.
 * Effect: traffic originating from the USB PC (10.0.0.x) and destined for
 * WiFi clients is masqueraded as 192.168.4.1.  WiFi clients see it from the
 * Pico's WiFi IP, and reply paths are tracked automatically by the NAT table.
 *
 * For traffic in the other direction (WiFi → USB), add a host route on the
 * WiFi client:
 *   Linux:   ip route add 10.0.0.0/24 via 192.168.4.1
 *   macOS:   sudo route add 10.0.0.0/24 192.168.4.1
 *   Windows: route add 10.0.0.0 mask 255.255.255.0 192.168.4.1
 *
 * ip4_napt.c must be compiled (CMakeLists.txt adds it from the SDK lwIP tree).
 * If it is not available, IP_FORWARD still routes packets — both sides just
 * need explicit routes.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "usb_net.h"
#include "wifi_ap.h"
#include "network.h"

// ip4_napt.h is only present when ip4_napt.c is compiled.
#ifndef NO_IP_NAPT
#  include "lwip/ip4_napt.h"
#endif

void network_init(void) {
    printf("Network: IP forwarding enabled\n");

#ifndef NO_IP_NAPT
    // Initialise the NAPT engine (allocates the session table).
    ip_napt_init(IP_NAPT_MAX);

    // Enable NAPT on the WiFi AP interface.
    // Packets being forwarded OUT through 192.168.4.1 are masqueraded.
    ip4_addr_t wifi_gw;
    IP4_ADDR(&wifi_gw, 192, 168, 4, 1);
    ip_napt_enable(wifi_gw.addr, 1);

    printf("Network: NAPT enabled on WiFi AP (192.168.4.1)\n");
    printf("Network: USB clients can reach WiFi clients transparently\n");
#else
    printf("Network: NAPT disabled (ip4_napt.c not compiled)\n");
    printf("Network: add static routes on both PCs — see README\n");
#endif
}

void network_print_status(void) {
    struct netif *usb  = usb_net_get_netif();
    struct netif *wifi = wifi_ap_get_netif();

    printf("--- Status ---\n");

    if (usb) {
        printf("USB  (%s): IP=%-16s link=%s if=%s\n",
               netif_is_up(usb)   ? "UP  " : "DOWN",
               ip4addr_ntoa(netif_ip4_addr(usb)),
               netif_is_link_up(usb) ? "UP" : "DOWN",
               usb->name);
    }

    if (wifi) {
        int rssi = 0;
        cyw43_wifi_get_rssi(&cyw43_state, &rssi);
        printf("WiFi (%s): IP=%-16s link=%s RSSI=%d dBm\n",
               netif_is_up(wifi)   ? "UP  " : "DOWN",
               ip4addr_ntoa(netif_ip4_addr(wifi)),
               netif_is_link_up(wifi) ? "UP" : "DOWN",
               rssi);
    }
}
