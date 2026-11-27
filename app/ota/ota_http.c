/*
 * HTTP Range GET over lwIP netconn for the OTA demo (see ota_http.h).
 * Only the OTA task calls it; the buffers are static.
 */
#include <string.h>

#include "lwip/api.h"
#include "lwip/ip_addr.h"

#include "ota_http.h"

static char req_buf[OTA_URL_MAX + 96U];
static char head_buf[OTA_HEAD_MAX];

const char *ota_get_status_str(ota_get_status_t st)
{
    static const char *const names[] = {
        "ok", "connect failed", "io error", "bad response head", "http status", "body too long"
    };
    return ((unsigned)st < sizeof names / sizeof names[0]) ? names[st] : "?";
}

/* Copies body bytes into the caller buffer; false if they do not fit. */
static bool put_body(uint8_t *body, size_t cap, size_t *have, const uint8_t *src, size_t n)
{
    if (n > cap - *have) {
        return false;
    }
    memcpy(&body[*have], src, n);
    *have += n;
    return true;
}

static ota_get_status_t exchange(struct netconn *c, const ota_url_t *u, uint32_t first, uint32_t last,
                                 uint8_t *body, size_t cap, size_t *body_len, ota_http_head_t *head)
{
    size_t rn = ota_http_range_request(req_buf, sizeof req_buf, u, first, last);
    if (rn == 0U || netconn_write(c, req_buf, rn, NETCONN_COPY) != ERR_OK) {
        return OTA_GET_IO;
    }

    size_t hlen = 0U;
    bool have_head = false;
    *body_len = 0U;
    for (;;) {
        struct netbuf *nb = NULL;
        err_t e = netconn_recv(c, &nb);
        if (e == ERR_CLSD) {
            break;                                   /* server closed: end of body */
        }
        if (e != ERR_OK) {
            return OTA_GET_IO;                       /* timeout or reset */
        }
        ota_get_status_t st = OTA_GET_OK;
        do {
            void *data;
            u16_t n;
            netbuf_data(nb, &data, &n);
            const uint8_t *src = data;
            if (!have_head) {
                size_t take = (n < sizeof head_buf - hlen) ? n : sizeof head_buf - hlen;
                memcpy(&head_buf[hlen], src, take);
                hlen += take;
                ota_head_status_t hs = ota_http_parse_head(head_buf, hlen, head);
                if (hs == OTA_HEAD_BAD) {
                    st = OTA_GET_HEAD;
                    break;
                }
                if (hs == OTA_HEAD_INCOMPLETE) {
                    continue;                        /* next part */
                }
                have_head = true;
                /* bytes after the blank line in head_buf, then the rest of this part */
                if (!put_body(body, cap, body_len, (const uint8_t *)&head_buf[head->head_len],
                              hlen - head->head_len) ||
                    !put_body(body, cap, body_len, src + take, (size_t)n - take)) {
                    st = OTA_GET_TOO_LONG;
                    break;
                }
            } else if (!put_body(body, cap, body_len, src, n)) {
                st = OTA_GET_TOO_LONG;
                break;
            }
        } while (netbuf_next(nb) >= 0);
        netbuf_delete(nb);
        if (st != OTA_GET_OK) {
            return st;
        }
        if (have_head && head->has_length && *body_len >= head->content_length) {
            break;                                   /* whole body read */
        }
    }
    if (!have_head) {
        return OTA_GET_HEAD;
    }
    if (head->status != 200 && head->status != 206) {
        return OTA_GET_STATUS;
    }
    if (head->has_length && *body_len != head->content_length) {
        return OTA_GET_IO;                           /* connection closed early */
    }
    return OTA_GET_OK;
}

ota_get_status_t ota_http_get_range(const ota_url_t *u, uint32_t first, uint32_t last,
                                    uint8_t *body, size_t cap, size_t *body_len,
                                    ota_http_head_t *head)
{
    ip_addr_t ip;
    if (!ipaddr_aton(u->host, &ip)) {
        return OTA_GET_CONNECT;
    }
    struct netconn *c = netconn_new(NETCONN_TCP);
    if (c == NULL) {
        return OTA_GET_CONNECT;
    }
    netconn_set_recvtimeout(c, (int)OTA_HTTP_TIMEOUT_MS);
    netconn_set_sendtimeout(c, (int)OTA_HTTP_TIMEOUT_MS);
    ota_get_status_t st = OTA_GET_CONNECT;
    if (netconn_connect(c, &ip, u->port) == ERR_OK) {
        st = exchange(c, u, first, last, body, cap, body_len, head);
        (void)netconn_close(c);
    }
    (void)netconn_delete(c);
    return st;
}
