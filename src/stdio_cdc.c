/*
 * stdio_cdc.c — route printf → TinyUSB CDC-ACM interface 0
 *
 * Call stdio_cdc_init() once after tusb_init().
 * Output is dropped silently until the host opens the CDC port.
 * Never calls tud_task() — safe to use from any context.
 */

#include "pico/stdio/driver.h"   // full struct stdio_driver definition
#include "tusb.h"

static void cdc_out_chars(const char *buf, int len) {
    if (!tud_cdc_connected()) return;
    while (len > 0) {
        uint32_t n = tud_cdc_write(buf, (uint32_t)len);
        if (n == 0) break;   // TX buffer full — drop remaining rather than block
        buf += n;
        len -= (int)n;
    }
    tud_cdc_write_flush();
}

static stdio_driver_t stdio_cdc_driver = {
    .out_chars = cdc_out_chars,
};

void stdio_cdc_init(void) {
    stdio_set_driver_enabled(&stdio_cdc_driver, true);
}
