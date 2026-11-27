/*
 * coreMQTT transport interface over a plain lwIP TCP netconn.
 *
 * coreMQTT asks for exact byte counts (header, then remaining length, then payload);
 * lwIP hands over whole netbufs. The context keeps the current netbuf and the read
 * offset, so one received segment can feed several recv() calls.
 *
 *   recv(): copies what is buffered; waits at most MQTT_TRANSPORT_RECV_MS for new
 *           data when nothing is buffered; 0 = no data yet, -1 = connection error.
 *   send(): netconn_write_partly() with a send timeout; returns bytes accepted
 *           (may be fewer than asked), 0 on timeout, -1 on error.
 *
 * Only the MQTT task uses a context. No TLS: port 1883 on a trusted LAN only.
 */
#ifndef MQTT_TRANSPORT_LWIP_H
#define MQTT_TRANSPORT_LWIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lwip/api.h"

#include "transport_interface.h"

#define MQTT_TRANSPORT_RECV_MS   10      /* idle poll inside MQTT_ProcessLoop */
#define MQTT_TRANSPORT_SEND_MS   2000

struct NetworkContext {
    struct netconn *conn;
    struct netbuf  *rx;          /* netbuf being consumed, NULL if none */
    uint16_t        rx_off;      /* offset inside the current netbuf part */
};

/* Connects to an IPv4 literal ("192.168.1.10"). Blocks until the TCP handshake ends:
   at most about 20 s when the host does not answer (lwIP: 6 SYN retries, 3 s RTO). */
bool    mqtt_transport_connect(NetworkContext_t *ctx, const char *ipv4, uint16_t port);
void    mqtt_transport_close(NetworkContext_t *ctx);

int32_t mqtt_transport_recv(NetworkContext_t *ctx, void *buf, size_t len);
int32_t mqtt_transport_send(NetworkContext_t *ctx, const void *buf, size_t len);

#endif /* MQTT_TRANSPORT_LWIP_H */
