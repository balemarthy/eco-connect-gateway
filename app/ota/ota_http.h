/*
 * OTA transport on the target: one HTTP GET with a Range header per block, over an
 * lwIP netconn (one TCP connection per request, "Connection: close").
 */
#ifndef OTA_HTTP_H
#define OTA_HTTP_H

#include <stddef.h>
#include <stdint.h>

#include "ota_http_msg.h"

typedef enum {
    OTA_GET_OK = 0,
    OTA_GET_CONNECT,       /* no TCP connection */
    OTA_GET_IO,            /* send/receive failed or timed out */
    OTA_GET_HEAD,          /* response head malformed or too long */
    OTA_GET_STATUS,        /* not 200/206 */
    OTA_GET_TOO_LONG       /* body larger than the caller's buffer */
} ota_get_status_t;

/* GET bytes first..last of u->path. On OTA_GET_OK, body[0..*body_len) holds the
   payload and *head the parsed response head (check has_range / range_first). */
ota_get_status_t ota_http_get_range(const ota_url_t *u, uint32_t first, uint32_t last,
                                    uint8_t *body, size_t cap, size_t *body_len,
                                    ota_http_head_t *head);

const char *ota_get_status_str(ota_get_status_t st);

#endif /* OTA_HTTP_H */
