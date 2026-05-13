#pragma once

#include "lwip/netif.h"

// Initialize USB network interface (10.0.0.1/24) and its DHCP server.
void usb_net_init(void);

// Return pointer to the lwIP netif for the USB side.
struct netif *usb_net_get_netif(void);
