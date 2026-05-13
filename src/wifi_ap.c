/*
 * wifi_ap.c — CYW43439 access-point setup
 *
 * AP parameters (compile-time via -D):
 *   WIFI_SSID     = "PicoBridge"
 *   WIFI_PASSWORD = "picobridge123"
 *   WIFI_CHANNEL  = 6
 *
 * Subnet: 192.168.4.0/24, Pico gateway 192.168.4.1
 * DHCP:   hands out 192.168.4.2 – 192.168.4.9
 */

#include <stdio.h>
#include "pico/cyw43_arch.h"
#include "cyw43.h"              // cyw43_state, CYW43_ITF_AP
#include "lwip/ip4_addr.h"
#include "dhcpserver.h"
#include "wifi_ap.h"

static dhcp_server_t wifi_dhcp;

void wifi_ap_init(void) {
    cyw43_arch_enable_ap_mode(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK
    );

    ip4_addr_t gw, mask;
    IP4_ADDR(&gw,   192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);

    // Pass the WiFi AP netif so responses go out WiFi, not USB.
    dhcp_server_init(&wifi_dhcp, &gw, &mask, &cyw43_state.netif[CYW43_ITF_AP]);

    printf("WiFi AP : SSID=%s  IP=192.168.4.1/24\n", WIFI_SSID);
    printf("WiFi AP : channel %d, WPA2-AES-PSK\n", WIFI_CHANNEL);
}

void wifi_ap_deinit(void) {
    dhcp_server_deinit(&wifi_dhcp);
    cyw43_arch_disable_ap_mode();
}

struct netif *wifi_ap_get_netif(void) {
    // The CYW43 driver registers the AP netif in cyw43_state.netif[CYW43_ITF_AP].
    return &cyw43_state.netif[CYW43_ITF_AP];
}
