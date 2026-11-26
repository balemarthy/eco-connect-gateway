#ifndef NET_INIT_H
#define NET_INIT_H

#include <stdbool.h>

/* Starts the lwIP tcpip thread. Safe to call before vTaskStartScheduler(). */
void net_init(void);
bool net_stack_ready(void);

#endif /* NET_INIT_H */
