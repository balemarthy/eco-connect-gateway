/*
 * lwIP 2.2.0 options for the Eco-Connect gateway (STM32F411RE, 128 KB RAM).
 *
 * Sizing (see docs/memory-budget.md for the measured numbers):
 *   MEM_SIZE 10 KB            heap for TX pbufs (PBUF_RAM) and TCP segments
 *   PBUF_POOL 10 x 1536 B     RX frames from the ENC28J60
 *   4 TCP PCBs, 1 listener    HTTP server handles one request at a time
 * Everything here is static (lwIP memp/mem). The FreeRTOS port creates its
 * threads, mailboxes and semaphores from heap_4.
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ---- OS integration --------------------------------------------------------- */
#define NO_SYS                          0
#define SYS_LIGHTWEIGHT_PROT            1
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   0
#define LWIP_NETCONN_SEM_PER_THREAD     0
#define LWIP_COMPAT_MUTEX               0

#define TCPIP_THREAD_NAME               "tcpip"
#define TCPIP_THREAD_STACKSIZE          2048       /* bytes */
#define TCPIP_THREAD_PRIO               4
#define TCPIP_MBOX_SIZE                 8
#define DEFAULT_THREAD_STACKSIZE        1024
#define DEFAULT_THREAD_PRIO             3
#define DEFAULT_TCP_RECVMBOX_SIZE       6
#define DEFAULT_UDP_RECVMBOX_SIZE       4
#define DEFAULT_RAW_RECVMBOX_SIZE       4
#define DEFAULT_ACCEPTMBOX_SIZE         4

/* ---- memory ----------------------------------------------------------------- */
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (10 * 1024)
#define MEMP_NUM_PBUF                   12
#define MEMP_NUM_RAW_PCB                0
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                4
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_NETBUF                 6
#define MEMP_NUM_NETCONN                6
#define MEMP_NUM_TCPIP_MSG_API          8
#define MEMP_NUM_TCPIP_MSG_INPKT        8
#define PBUF_POOL_SIZE                  10
#define PBUF_POOL_BUFSIZE               1536

/* ---- protocols -------------------------------------------------------------- */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define ARP_TABLE_SIZE                  8
#define ARP_QUEUEING                    1
#define ETHARP_SUPPORT_VLAN             0
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        0
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DHCP                       1
#define LWIP_AUTOIP                     0
#define LWIP_DNS                        0
#define LWIP_IGMP                       0
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0

#define TCP_MSS                         1460
#define TCP_WND                         (2 * TCP_MSS)
#define TCP_SND_BUF                     (2 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * TCP_SND_BUF) / TCP_MSS)
#define TCP_LISTEN_BACKLOG              1
#define LWIP_TCP_KEEPALIVE              0

/* ---- APIs ------------------------------------------------------------------- */
#define LWIP_NETCONN                    1
#define LWIP_SOCKET                     0
#define LWIP_NETIF_API                  0
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SO_SNDTIMEO                1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_HOSTNAME             1

/* ---- checksums: all in software (the ENC28J60 has no checksum offload) ------ */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1
#define CHECKSUM_CHECK_ICMP             1

/* ---- statistics / debug ----------------------------------------------------- */
#define LWIP_STATS                      0
#define LWIP_STATS_DISPLAY              0

#endif /* LWIPOPTS_H */
