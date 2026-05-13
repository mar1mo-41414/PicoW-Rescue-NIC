#pragma once

#include "lwip/netif.h"

// Start WiFi AP (192.168.4.1/24) and DHCP server.
void wifi_ap_init(void);

// Stop WiFi AP and DHCP server.
void wifi_ap_deinit(void);

// Return pointer to the lwIP netif for the WiFi AP interface.
struct netif *wifi_ap_get_netif(void);
