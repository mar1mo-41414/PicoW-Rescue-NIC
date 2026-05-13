#pragma once

// Setup IP forwarding and NAPT between USB (10.0.0.0/24) and WiFi (192.168.4.0/24).
// Must be called after usb_net_init() and wifi_ap_init().
void network_init(void);

// Print current routing / link status to UART.
void network_print_status(void);
