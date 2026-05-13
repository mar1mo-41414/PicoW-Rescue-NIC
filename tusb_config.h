#pragma once

// =============================================================================
// TinyUSB device configuration for PicoW-NIC
// Composite: CDC-ACM (debug UART) + CDC-NCM (USB Ethernet)
// =============================================================================

// MCU / OS
#define CFG_TUSB_MCU            OPT_MCU_RP2040
#define CFG_TUSB_OS             OPT_OS_NONE      // cooperative poll, no RTOS
#define CFG_TUSB_DEBUG          0

// Device mode only (no host)
#define CFG_TUD_ENABLED         1
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE

// Endpoint 0 packet size
#define CFG_TUD_ENDPOINT0_SIZE  64

// ---------------------------------------------------------------------------
// CDC-ACM — used as debug/console UART (replaces stdio USB)
// ---------------------------------------------------------------------------
#define CFG_TUD_CDC             1
#define CFG_TUD_CDC_RX_BUFSIZE  256
#define CFG_TUD_CDC_TX_BUFSIZE  256
#define CFG_TUD_CDC_EP_BUFSIZE  64

// ---------------------------------------------------------------------------
// CDC-NCM — USB Ethernet (Linux/macOS native; Windows 10+ native)
// NCM is preferred over ECM: better throughput, aggregated frames.
// For older Windows use RNDIS (see README for RNDIS build option).
// ---------------------------------------------------------------------------
#define CFG_TUD_NET             1
#define CFG_TUD_NET_ENDPOINT_SIZE  64
#define CFG_TUD_NET_MTU         1500    // max IP payload (no Ethernet FCS)

// Unused classes
#define CFG_TUD_HID             0
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0
