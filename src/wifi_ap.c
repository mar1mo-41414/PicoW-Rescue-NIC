/*
 * wifi_ap.c — CYW43439 access point + DHCP (fixed IP 192.168.4.10)
 */

#include <stdio.h>
#include "pico/cyw43_arch.h"
#include "cyw43.h"
#include "lwip/ip_addr.h"
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

    ip4_addr_t gw, mask, client_ip;
    IP4_ADDR(&gw,        192, 168, 4,  1);
    IP4_ADDR(&mask,      255, 255, 255, 0);
    IP4_ADDR(&client_ip, 192, 168, 4,  10);   // fixed IP for WiFi clients

    ip_addr_t gw_ia, mask_ia;
    ip_addr_copy_from_ip4(gw_ia,   gw);
    ip_addr_copy_from_ip4(mask_ia, mask);

    struct netif *wifi_if = &cyw43_state.netif[CYW43_ITF_AP];
    dhcp_server_init(&wifi_dhcp, &gw_ia, &mask_ia, &client_ip, wifi_if);

    printf("WiFi AP: SSID=%s  Pico=192.168.4.1  Client=192.168.4.10 (DHCP fixed)\n",
           WIFI_SSID);
}

void wifi_ap_deinit(void) {
    dhcp_server_deinit(&wifi_dhcp);
    cyw43_arch_disable_ap_mode();
}

struct netif *wifi_ap_get_netif(void) {
    return &cyw43_state.netif[CYW43_ITF_AP];
}
