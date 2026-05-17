/*
 * main.c — PicoW-NIC entry point
 *
 * Initialisation order:
 *   1. stdio (UART debug)
 *   2. cyw43_arch (WiFi chip + lwIP poll mode)
 *   3. tusb_init  (TinyUSB device)
 *   4. wifi_ap_init  → AP mode + DHCP on 192.168.4.0/24
 *   5. usb_net_init  → CDC-NCM netif + DHCP on 10.0.0.0/24
 *   6. network_init  → IP_FORWARD + NAPT
 *
 * Main loop (cooperative, no RTOS):
 *   tud_task()          — USB events (enumeration, transfers)
 *   cyw43_arch_poll()   — WiFi driver + lwIP rx/tx
 *   sys_check_timeouts()— lwIP timer callbacks (TCP keepalive, ARP, DHCP)
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/timeouts.h"
#include "tusb.h"

#include "wifi_ap.h"
#include "usb_net.h"
#include "network.h"

void stdio_cdc_init(void);   // src/stdio_cdc.c

static void status_task(void) {
    static uint32_t next_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now < next_ms) return;
    next_ms = now + 15000;   // print every 15 s
    network_print_status();
}

int main(void) {
    // UART on GP0/GP1 as fallback (useful if you have a serial adapter).
    stdio_init_all();

    // TinyUSB device stack — must be up before we can use CDC-ACM.
    tusb_init();

    // Register the CDC stdio driver so printf → /dev/ttyACM*
    stdio_cdc_init();

    // Pump USB until CDC host opens the port (or 5 s timeout).
    // This ensures the boot banner is visible on /dev/ttyACM*.
    {
        uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 5000;
        while (!tud_cdc_connected() &&
               to_ms_since_boot(get_absolute_time()) < deadline) {
            tud_task();
            sleep_ms(1);
        }
    }

    printf("\n================================================\n");
    printf(" PicoW-NIC  USB-WiFi Bridge\n");
    printf(" USB subnet : 10.0.0.0/24\n");
    printf(" WiFi subnet: 192.168.4.0/24  SSID=" WIFI_SSID "\n");
    printf("================================================\n\n");

    // CYW43 + lwIP in cooperative poll mode (NO_SYS=1).
    // Must be called before wifi_ap_init / network_init.
    if (cyw43_arch_init() != 0) {
        printf("FATAL: cyw43_arch_init failed\n");
        for (;;) tight_loop_contents();
    }

    // WiFi AP: 192.168.4.1/24
    wifi_ap_init();

    // USB ECM/NCM: 10.0.0.1/24
    usb_net_init();

    // IP forwarding + NAPT
    network_init();

    printf("\nReady — connect PC to USB and/or WiFi AP \"%s\"\n\n", WIFI_SSID);

    // Cooperative main loop — all drivers are non-blocking pollers.
    while (true) {
        tud_task();              // TinyUSB: USB enumeration + NCM rx/tx
        cyw43_arch_poll();       // CYW43: WiFi driver + lwIP rx/tx
        sys_check_timeouts();    // lwIP: timers (TCP, ARP, DHCP renewal)
        status_task();           // periodic UART status
    }

    return 0;
}
