/*
 * HTTP/1.1 pieces for the OTA download: URL parsing, the Range request line and
 * parsing of the response head. Pure C (host tested); the socket side is ota_http.c.
 *
 *   URL      http://<IPv4>[:port]/<path>     (no DNS in this lwIP build, no https)
 *   request  GET <path> HTTP/1.1 / Host / Range: bytes=<first>-<last> / Connection: close
 *   reply    206 Partial Content + Content-Range: bytes <first>-<last>/<total>
 *            or 200 OK with the whole file (server without Range support)
 */
#ifndef OTA_HTTP_MSG_H
#define OTA_HTTP_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

#define OTA_HOST_MAX   16U            /* "255.255.255.255" + NUL */
#define OTA_HEAD_MAX   512U           /* response head we are prepared to read */

typedef struct {
    char     host[OTA_HOST_MAX];
    uint16_t port;
    char     path[OTA_URL_MAX];
} ota_url_t;

/* true for http://a.b.c.d[:port]/path with a valid dotted IPv4 and port 1..65535 */
bool   ota_url_parse(const char *url, ota_url_t *out);

/* Range request for bytes first..last (inclusive). Returns length, 0 if buf is too small. */
size_t ota_http_range_request(char *buf, size_t size, const ota_url_t *u, uint32_t first, uint32_t last);

typedef enum {
    OTA_HEAD_INCOMPLETE = 0,   /* no blank line yet */
    OTA_HEAD_OK,
    OTA_HEAD_BAD               /* malformed, chunked, or longer than OTA_HEAD_MAX */
} ota_head_status_t;

typedef struct {
    int      status;           /* 200, 206, 404, ... */
    bool     has_length;
    uint32_t content_length;
    bool     has_range;        /* Content-Range: bytes a-b/total with a numeric total */
    uint32_t range_first;
    uint32_t range_last;
    uint32_t range_total;
    size_t   head_len;         /* bytes up to and including the blank line */
} ota_http_head_t;

ota_head_status_t ota_http_parse_head(const char *buf, size_t len, ota_http_head_t *h);

#endif /* OTA_HTTP_MSG_H */
