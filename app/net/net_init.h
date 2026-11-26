/*
 * Network bring-up: lwIP tcpip thread + the ENC28J60 netif.
 *
 * Static IPv4 by default (NET_STATIC_IP). Build with `make DHCP=1` to use DHCP
 * (the static address is then only the starting value until a lease arrives).
 */
#ifndef NET_INIT_H
#define NET_INIT_H

#include <stdbool.h>
#include <stddef.h>

#define NET_STATIC_IP    "192.168.1.50"
#define NET_STATIC_MASK  "255.255.255.0"
#define NET_STATIC_GW    "192.168.1.1"

#ifndef NET_USE_DHCP
#define NET_USE_DHCP 0
#endif

/* Starts the lwIP tcpip thread; the netif is added from inside it.
   Safe to call before vTaskStartScheduler(). */
void net_init(void);

bool net_stack_ready(void);        /* tcpip thread running */
bool net_is_up(void);              /* netif added, link up and an address assigned */
bool net_link_up(void);
/* Current IPv4 address as text ("0.0.0.0" if none). */
void net_ip_string(char *buf, size_t size);

#endif /* NET_INIT_H */
