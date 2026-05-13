#pragma once

// =============================================================================
// TinyUSB device configuration for PicoW-NIC
// Composite: CDC-ACM (debug UART) + CDC-NCM (USB Ethernet)
//
// NOTE: pico-sdk sets these via CMake/BSP — do NOT redefine here:
//   CFG_TUSB_MCU      = OPT_MCU_RP2040
//   CFG_TUSB_OS       = OPT_OS_PICO
//   CFG_TUD_ENABLED   = 1
// =============================================================================

// CFG_TUSB_RHPORT0_MODE is only defined by pico_stdio_usb, which we disable
// (USB is used for CDC-NCM, not stdio).  Define it here so that tusb_option.h
// derives CFG_TUD_ENABLED=1 correctly.
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE

#define CFG_TUSB_DEBUG          0

// Endpoint 0 packet size
#define CFG_TUD_ENDPOINT0_SIZE  64

// ---------------------------------------------------------------------------
// CDC-ACM — debug/console UART (replaces stdio USB which is disabled)
// ---------------------------------------------------------------------------
#define CFG_TUD_CDC             1
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256
#define CFG_TUD_CDC_EP_BUFSIZE  64

// ---------------------------------------------------------------------------
// CDC-NCM — USB Ethernet
//   CFG_TUD_NET was renamed to CFG_TUD_ECM_RNDIS (ECM/RNDIS) and
//   CFG_TUD_NCM (NCM) in TinyUSB 0.15+.
//   pico-sdk bundles ≥ 0.15, so use the new name.
//   NCM is preferred: better throughput, same OS support as ECM.
// ---------------------------------------------------------------------------
#define CFG_TUD_NCM             1
// CFG_TUD_NET_ENDPOINT_SIZE is defined in net_device.h — do not redefine.

// Unused classes
#define CFG_TUD_ECM_RNDIS       0
#define CFG_TUD_HID             0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0
