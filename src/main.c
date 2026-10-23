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
#include "cyw43.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"
#include "tusb.h"

#include "wifi_ap.h"
#include "usb_net.h"
#include "network.h"
#include "status_led.h"
#include "version.h"

void stdio_cdc_init(void);   // src/stdio_cdc.c

static void status_task(void) {
    static uint32_t next_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now < next_ms) return;
    next_ms = now + 15000;   // print every 15 s
    network_print_status();
}

// Evaluate the current runtime condition and drive the status LED.
// Throttled to 4x/sec — cyw43_wifi_ap_get_stas() talks to the WiFi driver
// and there is no need to re-check faster than a human can read a blink.
static void led_update_task(void) {
    static uint32_t next_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now < next_ms) return;
    next_ms = now + 250;

    struct netif *wifi = wifi_ap_get_netif();
    bool wifi_up = wifi && netif_is_link_up(wifi);
    bool usb_up  = tud_ready();

    int num_stas = 0;
    static uint8_t sta_macs[32 * 6];   // CYW43 AP max stations; matches pico-sdk examples
    cyw43_wifi_ap_get_stas(&cyw43_state, &num_stas, sta_macs);

    led_state_t s;
    if (!wifi_up)           s = LED_STATE_WIFI_DOWN;    // code 3
    else if (!usb_up)       s = LED_STATE_USB_WAIT;     // code 2
    else if (num_stas <= 0) s = LED_STATE_CLIENT_WAIT;  // 1 Hz blink
    else                    s = LED_STATE_ACTIVE;       // near-solid
    status_led_set(s);
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
    printf(" Pico Rescue Bridge " FW_VERSION "\n");
    printf(" RP2040 USB Rescue NIC\n");
    printf("================================================\n");
    printf(" Build: " __DATE__ " " __TIME__ "\n");
    printf(" Git:   " GIT_HASH "\n");
    printf(" USB subnet : 10.0.0.0/24\n");
    printf(" WiFi subnet: 192.168.4.0/24  SSID=" WIFI_SSID "\n");
    printf("================================================\n\n");

    // CYW43 + lwIP in cooperative poll mode (NO_SYS=1).
    // Must be called before wifi_ap_init / network_init.
    if (cyw43_arch_init() != 0) {
        // The status LED is wired to the CYW43 chip, so if the chip never
        // came up we cannot blink an error code — the board just stays dark.
        // "Powered but completely dark for >1 s" is itself the fatal signal.
        printf("FATAL: cyw43_arch_init failed (WiFi chip did not initialise)\n");
        for (;;) tight_loop_contents();
    }

    // Onboard status LED (driven through the CYW43 chip — hence only now).
    status_led_init();

    // WiFi AP: 192.168.4.1/24
    wifi_ap_init();

    // USB ECM/NCM: 10.0.0.1/24
    usb_net_init();

    // IP forwarding + NAPT
    network_init();

    printf("\nReady — connect PC to USB and/or WiFi AP \"%s\"\n", WIFI_SSID);
    printf("LED: near-solid=OK  1Hz blink=waiting for WiFi client\n");
    printf("LED: long+2short=USB host not seen  long+3short=WiFi AP down\n");
    printf("     (see docs/troubleshooting.md for the full LED code table)\n\n");

    // Cooperative main loop — all drivers are non-blocking pollers.
    while (true) {
        tud_task();              // TinyUSB: USB enumeration + NCM rx/tx
        cyw43_arch_poll();       // CYW43: WiFi driver + lwIP rx/tx
        sys_check_timeouts();    // lwIP: timers (TCP, ARP, DHCP renewal)
        status_task();           // periodic UART status
        led_update_task();       // evaluate state → status LED
        status_led_task();       // render LED blink pattern
    }

    return 0;
}
