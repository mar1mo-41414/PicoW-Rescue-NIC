#pragma once

#include <stdint.h>

// Interface numbers — must match usb_descriptors.c
enum {
    ITF_NUM_CDC_ACM = 0,   // CDC-ACM control  (debug UART)
    ITF_NUM_CDC_ACM_DATA,  // CDC-ACM data
    ITF_NUM_NCM,           // CDC-NCM control  (USB Ethernet)
    ITF_NUM_NCM_DATA,      // CDC-NCM data
    ITF_NUM_TOTAL
};

// Endpoint addresses — must match usb_descriptors.c
#define EPNUM_CDC_NOTIF   0x81u   // CDC-ACM interrupt IN
#define EPNUM_CDC_OUT     0x02u   // CDC-ACM bulk OUT
#define EPNUM_CDC_IN      0x82u   // CDC-ACM bulk IN
#define EPNUM_NCM_NOTIF   0x83u   // CDC-NCM interrupt IN
#define EPNUM_NCM_OUT     0x04u   // CDC-NCM bulk OUT
#define EPNUM_NCM_IN      0x84u   // CDC-NCM bulk IN
