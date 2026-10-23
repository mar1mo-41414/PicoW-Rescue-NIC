/*
 * status_led.c — Onboard LED status indicator (blink-pattern state machine)
 *
 * See status_led.h for the state/pattern overview.
 *
 * Implementation: each state maps to a small table of {level, duration}
 * steps. status_led_task() walks the table using to_ms_since_boot() and
 * loops back to the start, so the whole thing is non-blocking and fits the
 * cooperative main loop (no RTOS, no timers, no interrupts).
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "status_led.h"

typedef struct {
    bool     on;   // LED level for this step
    uint16_t ms;   // how long to hold it
} led_step_t;

// --- Pattern timing (milliseconds) -----------------------------------------
#define T_LONG_ON   600   // a "long" flash
#define T_SHORT_ON  150   // a "short" flash
#define T_GAP       250   // off-gap between flashes within a code
#define T_CODE_GAP 1300   // off-gap before the code repeats

// Solid on. A single long "on" step is enough; the task loops it.
static const led_step_t pat_boot[] = {
    { true, 1000 },
};

// Fully operational: essentially on, with a brief wink off every ~2 s so a
// healthy board is distinguishable from a hung one (steady, never-changing
// LED). Reads as "lit".
static const led_step_t pat_active[] = {
    { true, 1850 },
    { false, 100 },
};

// Up and waiting for a WiFi client: even, obvious 1 Hz blink.
static const led_step_t pat_client_wait[] = {
    { true, 500 },
    { false, 500 },
};

// Code 2 — USB host not seen: 1 long + 2 short, then a gap, repeat.
static const led_step_t pat_usb_wait[] = {
    { true, T_LONG_ON  }, { false, T_GAP },
    { true, T_SHORT_ON }, { false, T_GAP },
    { true, T_SHORT_ON }, { false, T_CODE_GAP },
};

// Code 3 — WiFi AP down: 1 long + 3 short, then a gap, repeat.
static const led_step_t pat_wifi_down[] = {
    { true, T_LONG_ON  }, { false, T_GAP },
    { true, T_SHORT_ON }, { false, T_GAP },
    { true, T_SHORT_ON }, { false, T_GAP },
    { true, T_SHORT_ON }, { false, T_CODE_GAP },
};

#define PAT(arr) (arr), (sizeof(arr) / sizeof((arr)[0]))

static const led_step_t *cur_pat = pat_boot;
static uint32_t          cur_len = 1;
static led_state_t       cur_state = LED_STATE_BOOT;
static uint32_t          step_idx = 0;
static uint32_t          step_until_ms = 0;   // 0 → apply current step now
static bool              inited = false;

static void load_pattern(led_state_t state) {
    switch (state) {
        case LED_STATE_ACTIVE:      cur_pat = PAT(pat_active);      break;
        case LED_STATE_CLIENT_WAIT: cur_pat = PAT(pat_client_wait); break;
        case LED_STATE_USB_WAIT:    cur_pat = PAT(pat_usb_wait);    break;
        case LED_STATE_WIFI_DOWN:   cur_pat = PAT(pat_wifi_down);   break;
        case LED_STATE_BOOT:
        default:                    cur_pat = PAT(pat_boot);        break;
    }
    step_idx = 0;
    step_until_ms = 0;   // force status_led_task() to apply step 0 immediately
}

void status_led_init(void) {
    // BOOT is solid-on; light it right away so the board shows life during
    // the (sub-second) init sequence before the main loop starts pumping
    // status_led_task().
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    cur_state = LED_STATE_BOOT;
    load_pattern(cur_state);
    inited = true;
}

void status_led_set(led_state_t state) {
    if (!inited || state == cur_state) return;
    cur_state = state;
    load_pattern(state);
}

void status_led_task(void) {
    if (!inited) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (step_until_ms != 0 && now < step_until_ms) return;

    const led_step_t *st = &cur_pat[step_idx];
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, st->on);
    step_until_ms = now + st->ms;
    step_idx = (step_idx + 1) % cur_len;
}
