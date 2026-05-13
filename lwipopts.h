#pragma once

// =============================================================================
// lwIP configuration for PicoW-NIC
// Targets: 264 KB RP2040 SRAM.  Keep heap + pools under ~48 KB total.
// =============================================================================

// --- Scheduler / OS ---
#define NO_SYS                      1   // cooperative poll, no RTOS
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

// --- Memory ---
#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4
// 20 KB dynamic heap: lwIP allocs TCP PCBs, ARP cache, etc. from here.
#define MEM_SIZE                    (20 * 1024)

// pbuf pool: 20 frames × 1514 B ≈ 30 KB
// Keep PBUF_POOL_SIZE modest — CYW43 driver also has its own buffers.
#define PBUF_POOL_SIZE              20
#define PBUF_POOL_BUFSIZE           1514

// --- Protocol enable ---
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DHCP                   1
#define LWIP_DNS                    0   // not needed for bridge

// --- IP forwarding ---
// IP_FORWARD=1 lets lwIP route packets between USB and WiFi netifs.
// NAPT is implemented in src/nat.c (ip4_napt.c absent from this SDK's lwIP).
#define IP_FORWARD                  1

// --- TCP tuning (small footprint) ---
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_ARP_QUEUE          6
#define LWIP_TCP_KEEPALIVE          1

// --- Netif callbacks ---
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1   // simplifies USB xmit path

// --- DHCP options ---
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

// --- Checksum ---
#define LWIP_CHKSUM_ALGORITHM       3

// --- lwIP hook for custom NAPT (src/nat.c) ---
// Declare the hook function here; implementation is in nat.c.
// The hook is called for every incoming IPv4 packet.
struct pbuf;
struct netif;
int nat_ip4_input_hook(struct pbuf *p, struct netif *inp);
#define LWIP_HOOK_IP4_INPUT(p, inp)  nat_ip4_input_hook(p, inp)

// --- Stats (disable in production to save RAM) ---
#define LWIP_STATS                  0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

// --- Debug (all off by default) ---
#define LWIP_DEBUG                  0
#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF
