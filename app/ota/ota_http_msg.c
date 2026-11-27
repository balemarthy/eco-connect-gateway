#include <stdio.h>
#include <string.h>

#include "ota_http_msg.h"

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Decimal number at *p, at most max; advances *p. False if no digit or overflow. */
static bool take_u32(const char **p, const char *end, uint32_t max, uint32_t *out)
{
    const char *s = *p;
    uint32_t v = 0U;
    if (s >= end || !is_digit(*s)) {
        return false;
    }
    while (s < end && is_digit(*s)) {
        uint32_t d = (uint32_t)(*s - '0');
        if (v > (max - d) / 10U) {
            return false;
        }
        v = v * 10U + d;
        s++;
    }
    *p = s;
    *out = v;
    return true;
}

static bool valid_ipv4(const char *s, size_t n)
{
    const char *p = s;
    const char *end = s + n;
    for (int part = 0; part < 4; part++) {
        uint32_t v;
        const char *start = p;
        if (!take_u32(&p, end, 255U, &v) || (p - start) > 3) {
            return false;
        }
        if (part < 3) {
            if (p >= end || *p != '.') {
                return false;
            }
            p++;
        }
    }
    return p == end;
}

bool ota_url_parse(const char *url, ota_url_t *out)
{
    static const char scheme[] = "http://";
    memset(out, 0, sizeof *out);
    if (url == NULL || strncmp(url, scheme, sizeof scheme - 1U) != 0) {
        return false;
    }
    const char *h = url + sizeof scheme - 1U;
    const char *slash = strchr(h, '/');
    if (slash == NULL) {
        return false;                                /* a file path is required */
    }
    const char *colon = memchr(h, ':', (size_t)(slash - h));
    const char *host_end = (colon != NULL) ? colon : slash;
    size_t hn = (size_t)(host_end - h);
    if (hn == 0U || hn >= sizeof out->host || !valid_ipv4(h, hn)) {
        return false;
    }
    uint32_t port = 80U;
    if (colon != NULL) {
        const char *p = colon + 1;
        if (!take_u32(&p, slash, 65535U, &port) || p != slash || port == 0U) {
            return false;
        }
    }
    size_t pn = strlen(slash);
    if (pn >= sizeof out->path) {
        return false;
    }
    for (size_t i = 0U; i < pn; i++) {
        if ((unsigned char)slash[i] <= ' ' || (unsigned char)slash[i] >= 0x7FU) {
            return false;                            /* no spaces / control bytes in the request line */
        }
    }
    memcpy(out->host, h, hn);
    out->host[hn] = '\0';
    memcpy(out->path, slash, pn + 1U);
    out->port = (uint16_t)port;
    return true;
}

size_t ota_http_range_request(char *buf, size_t size, const ota_url_t *u, uint32_t first, uint32_t last)
{
    int n = snprintf(buf, size,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "Range: bytes=%lu-%lu\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     u->path, u->host, (unsigned)u->port, (unsigned long)first, (unsigned long)last);
    return (n > 0 && (size_t)n < size) ? (size_t)n : 0U;
}

/* case-insensitive header name match at the start of a line */
static bool header_is(const char *line, size_t len, const char *name)
{
    size_t n = strlen(name);
    if (len < n) {
        return false;
    }
    for (size_t i = 0U; i < n; i++) {
        if (lower(line[i]) != name[i]) {
            return false;
        }
    }
    return true;
}

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

/* "bytes 0-1023/40960" */
static bool parse_content_range(const char *p, const char *end, ota_http_head_t *h)
{
    static const char unit[] = "bytes ";
    p = skip_ws(p, end);
    if ((size_t)(end - p) < sizeof unit - 1U || memcmp(p, unit, sizeof unit - 1U) != 0) {
        return false;
    }
    p += sizeof unit - 1U;
    uint32_t a, b, t;
    if (!take_u32(&p, end, 0xFFFFFFFFUL, &a) || p >= end || *p != '-') {
        return false;
    }
    p++;
    if (!take_u32(&p, end, 0xFFFFFFFFUL, &b) || p >= end || *p != '/') {
        return false;
    }
    p++;
    if (!take_u32(&p, end, 0xFFFFFFFFUL, &t)) {
        return false;                                /* "*" (unknown total) is not usable */
    }
    p = skip_ws(p, end);
    if (p != end || a > b || b >= t) {
        return false;
    }
    h->has_range = true;
    h->range_first = a;
    h->range_last = b;
    h->range_total = t;
    return true;
}

ota_head_status_t ota_http_parse_head(const char *buf, size_t len, ota_http_head_t *h)
{
    memset(h, 0, sizeof *h);

    const char *end = NULL;
    for (size_t i = 0U; i + 3U < len; i++) {
        if (buf[i] == '\r' && buf[i + 1U] == '\n' && buf[i + 2U] == '\r' && buf[i + 3U] == '\n') {
            end = &buf[i + 2U];                      /* end of the last header line incl. CRLF */
            h->head_len = i + 4U;
            break;
        }
    }
    if (end == NULL) {
        return (len >= OTA_HEAD_MAX) ? OTA_HEAD_BAD : OTA_HEAD_INCOMPLETE;
    }

    /* status line: HTTP/1.x SP ddd SP reason */
    const char *p = buf;
    const char *eol = memchr(p, '\r', (size_t)(end - p));
    if (eol == NULL || (eol - p) < 12 || memcmp(p, "HTTP/1.", 7) != 0 || p[8] != ' ') {
        return OTA_HEAD_BAD;
    }
    const char *s = p + 9;
    uint32_t code;
    if (!take_u32(&s, eol, 999U, &code) || (s - (p + 9)) != 3 || (s < eol && *s != ' ')) {
        return OTA_HEAD_BAD;
    }
    h->status = (int)code;

    for (p = eol + 2; p < end;) {
        const char *le = memchr(p, '\r', (size_t)(end - p));
        if (le == NULL) {
            return OTA_HEAD_BAD;
        }
        size_t ll = (size_t)(le - p);
        if (header_is(p, ll, "content-length:")) {
            const char *v = skip_ws(p + 15, le);
            uint32_t n;
            if (!take_u32(&v, le, 0xFFFFFFFFUL, &n) || skip_ws(v, le) != le) {
                return OTA_HEAD_BAD;
            }
            h->has_length = true;
            h->content_length = n;
        } else if (header_is(p, ll, "content-range:")) {
            if (!parse_content_range(p + 14, le, h)) {
                return OTA_HEAD_BAD;
            }
        } else if (header_is(p, ll, "transfer-encoding:")) {
            return OTA_HEAD_BAD;                     /* chunked bodies are not supported */
        }
        p = le + 2;
    }
    return OTA_HEAD_OK;
}
