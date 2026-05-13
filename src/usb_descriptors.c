/*
 * usb_descriptors.c — USB device / configuration / string descriptors
 *
 * Composite device:
 *   Interface 0+1 : CDC-ACM  (debug serial console)
 *   Interface 2+3 : CDC-NCM  (USB Ethernet, 10.0.0.0/24)
 *
 * USB VID/PID: placeholder values — replace with registered IDs for production.
 *
 * CDC-NCM is natively supported on:
 *   Linux   kernel ≥ 2.6.37  (cdc_ncm driver, automatic)
 *   macOS   10.6+             (automatic)
 *   Windows 10 build 1903+   (automatic, NCM driver)
 *   Windows older             → use RNDIS build (see README)
 */

#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_descriptors.h"

// ---------------------------------------------------------------------------
// USB VID / PID (placeholder — 0xCafe is TinyUSB test VID)
// ---------------------------------------------------------------------------
#define USB_VID 0xCAFEu
#define USB_PID 0x4010u

// ---------------------------------------------------------------------------
// Device descriptor
// bDeviceClass=0xEF + SubClass=0x02 + Protocol=0x01 → IAD composite
// ---------------------------------------------------------------------------
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

// ---------------------------------------------------------------------------
// Configuration descriptor
//
// String index assignments:
//   1  Manufacturer
//   2  Product
//   3  Serial (unique board ID — generated at runtime)
//   4  CDC-ACM interface name
//   5  CDC-NCM interface name
//   6  CDC-NCM MAC address ("020284696000" = 02:02:84:69:60:00)
//
// The MAC string MUST be 12 uppercase hex digits, no separators.
// It must match tud_network_mac_address[] in usb_net.c.
// ---------------------------------------------------------------------------
#define STRIDX_MANUFACTURER  1
#define STRIDX_PRODUCT       2
#define STRIDX_SERIAL        3
#define STRIDX_CDC_NAME      4
#define STRIDX_NCM_NAME      5
#define STRIDX_NCM_MAC       6

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_CDC_NCM_DESC_LEN)

uint8_t const desc_configuration[] = {
    // bNumInterfaces, bConfigurationValue, iConfiguration,
    // bmAttributes (bus-powered), bMaxPower (100 mA = 50 × 2mA)
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),

    // CDC-ACM: IF 0 (control) + IF 1 (data)
    //   notif EP 0x81 (8-byte INT-IN), data EP 0x02/0x82 (64-byte BULK)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_ACM, STRIDX_CDC_NAME,
                       EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // CDC-NCM: IF 2 (control) + IF 3 (data)
    //   notif EP 0x83 (64-byte INT-IN), data EP 0x04/0x84 (64-byte BULK)
    //   MTU = CFG_TUD_NET_MTU
    TUD_CDC_NCM_DESCRIPTOR(ITF_NUM_NCM, STRIDX_NCM_NAME, STRIDX_NCM_MAC,
                           EPNUM_NCM_NOTIF, 64,
                           EPNUM_NCM_OUT, EPNUM_NCM_IN, 64,
                           1500),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// ---------------------------------------------------------------------------
// String descriptors
// ---------------------------------------------------------------------------
static char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 }, // 0: language = English (0x0409)
    "PicoW-NIC",                  // 1: manufacturer
    "PicoW USB-WiFi Bridge",      // 2: product
    NULL,                         // 3: serial — filled from board unique ID
    "PicoW Debug",                // 4: CDC-ACM interface
    "PicoW Ethernet",             // 5: CDC-NCM interface
    "020284696000",               // 6: NCM MAC  02:02:84:69:60:00
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count = 0;

    if (index == 0) {
        // Language descriptor
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == STRIDX_SERIAL) {
        // Build 16-hex-char serial from RP2040 unique board ID
        pico_unique_board_id_t uid;
        pico_get_unique_board_id(&uid);
        chr_count = 16;
        static const char hex[] = "0123456789ABCDEF";
        for (uint8_t i = 0; i < 8; i++) {
            _desc_str[1 + i * 2]     = hex[uid.id[i] >> 4];
            _desc_str[1 + i * 2 + 1] = hex[uid.id[i] & 0x0F];
        }
    } else {
        if (index >= (uint8_t)(sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;
        const char *str = string_desc_arr[index];
        if (!str) return NULL;

        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // Header: type=STRING, total length in bytes
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
