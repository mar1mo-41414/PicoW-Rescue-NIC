/*
 * status_led.h — Onboard LED status indicator
 *
 * The Pico W has a single, single-colour LED wired to the CYW43439 WiFi
 * chip (GPIO is driven via cyw43_arch_gpio_put). We can therefore only
 * encode state in *blink patterns*, Raspberry Pi 4 PWR-LED style:
 * a number of LONG flashes followed by a number of SHORT flashes.
 *
 * Pattern summary (see docs/troubleshooting.md for the full table):
 *
 *   state              LED pattern                 meaning
 *   -----------------  --------------------------  ------------------------------
 *   BOOT               solid on                    initialising (transient, <1s)
 *   ACTIVE             on, blinks off once / ~2s   fully operational
 *   CLIENT_WAIT        even 1 Hz blink             up, waiting for a WiFi client
 *   USB_WAIT (code 2)  1 long + 2 short, repeat    USB host not seen (server side)
 *   WIFI_DOWN (code 3) 1 long + 3 short, repeat    WiFi AP is not up (firmware fault)
 *
 * NOTE: the LED itself lives on the WiFi chip. If cyw43_arch_init() fails
 * the LED cannot be driven at all — a Pico that is powered but stays
 * completely dark for more than ~1 second means the WiFi chip never came
 * up (fatal). There is no blink code for that case because we have no LED
 * to blink.
 */
#pragma once

typedef enum {
    LED_STATE_BOOT = 0,   // initialising
    LED_STATE_ACTIVE,     // USB host up + >=1 WiFi client connected
    LED_STATE_CLIENT_WAIT,// USB host up, no WiFi client yet
    LED_STATE_USB_WAIT,   // WiFi up, USB host not ready (code 2)
    LED_STATE_WIFI_DOWN,  // WiFi AP link down (code 3)
} led_state_t;

// Initialise the LED. Must be called AFTER cyw43_arch_init() succeeds,
// since the LED is driven through the CYW43 chip. Starts in BOOT state.
void status_led_init(void);

// Select the logical state to display. Cheap and idempotent — calling it
// every loop with the same state does nothing. Switching state restarts
// the pattern from its first step.
void status_led_set(led_state_t state);

// Advance the blink state machine. Non-blocking: call it as often as
// possible from the main loop. Does nothing until status_led_init() ran.
void status_led_task(void);
