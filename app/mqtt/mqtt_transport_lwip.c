/*
 * coreMQTT transport over an lwIP netconn (see mqtt_transport_lwip.h).
 */
#include <string.h>

#include "lwip/ip_addr.h"

#include "mqtt_transport_lwip.h"

bool mqtt_transport_connect(NetworkContext_t *ctx, const char *ipv4, uint16_t port)
{
    ip_addr_t ip;

    memset(ctx, 0, sizeof *ctx);
    if (!ipaddr_aton(ipv4, &ip)) {
        return false;                                /* no DNS in this build: literal only */
    }
    struct netconn *c = netconn_new(NETCONN_TCP);
    if (c == NULL) {
        return false;
    }
    netconn_set_recvtimeout(c, MQTT_TRANSPORT_RECV_MS);
    netconn_set_sendtimeout(c, MQTT_TRANSPORT_SEND_MS);
    if (netconn_connect(c, &ip, port) != ERR_OK) {
        (void)netconn_delete(c);
        return false;
    }
    ctx->conn = c;
    return true;
}

void mqtt_transport_close(NetworkContext_t *ctx)
{
    if (ctx->rx != NULL) {
        netbuf_delete(ctx->rx);
        ctx->rx = NULL;
    }
    if (ctx->conn != NULL) {
        (void)netconn_close(ctx->conn);
        (void)netconn_delete(ctx->conn);
        ctx->conn = NULL;
    }
    ctx->rx_off = 0U;
}

int32_t mqtt_transport_recv(NetworkContext_t *ctx, void *buf, size_t len)
{
    uint8_t *out = buf;
    size_t got = 0U;

    if (ctx->conn == NULL) {
        return -1;
    }
    while (got < len) {
        if (ctx->rx == NULL) {
            if (got > 0U) {
                break;                               /* hand over what we have, do not wait */
            }
            struct netbuf *nb = NULL;
            err_t e = netconn_recv(ctx->conn, &nb);
            if (e == ERR_TIMEOUT) {
                return 0;                            /* nothing yet: coreMQTT polls again */
            }
            if (e != ERR_OK) {
                return -1;                           /* closed or reset by the broker */
            }
            ctx->rx = nb;
            ctx->rx_off = 0U;
        }

        void *data;
        u16_t n;
        netbuf_data(ctx->rx, &data, &n);
        size_t avail = (n > ctx->rx_off) ? (size_t)(n - ctx->rx_off) : 0U;
        size_t take = (avail < (len - got)) ? avail : (len - got);
        memcpy(&out[got], (const uint8_t *)data + ctx->rx_off, take);
        got += take;
        ctx->rx_off = (uint16_t)(ctx->rx_off + take);

        if (ctx->rx_off >= n) {                      /* this part is used up */
            ctx->rx_off = 0U;
            if (netbuf_next(ctx->rx) < 0) {          /* and it was the last one */
                netbuf_delete(ctx->rx);
                ctx->rx = NULL;
            }
        }
    }
    return (int32_t)got;
}

int32_t mqtt_transport_send(NetworkContext_t *ctx, const void *buf, size_t len)
{
    size_t written = 0U;

    if (ctx->conn == NULL) {
        return -1;
    }
    err_t e = netconn_write_partly(ctx->conn, buf, len, NETCONN_COPY, &written);
    if (e == ERR_OK || written > 0U) {
        return (int32_t)written;
    }
    return (e == ERR_WOULDBLOCK) ? 0 : -1;           /* send timeout: try again later */
}
