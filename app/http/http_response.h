/*
 * HTTP/1.1 response header builder. Pure C.
 */
#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stddef.h>

#define HTTP_CT_JSON  "application/json"
#define HTTP_CT_HTML  "text/html; charset=utf-8"
#define HTTP_CT_TEXT  "text/plain; charset=utf-8"

typedef struct {
    int         status;          /* 200, 400, 404, ... */
    const char *content_type;
    const char *allow;           /* "Allow:" header for 405, else NULL */
    char       *body;            /* caller-owned buffer */
    size_t      body_size;       /* capacity of body */
    size_t      body_len;        /* bytes used */
} http_response_t;

const char *http_status_text(int status);

/* "HTTP/1.1 200 OK\r\nContent-Type: ...\r\nContent-Length: n\r\nConnection: close\r\n..."
   Returns the header length, 0 if it does not fit. */
size_t http_response_header(const http_response_t *r, char *out, size_t size);

/* Body helpers: append text; on overflow the body is truncated and 0 is returned. */
int  http_body_append(http_response_t *r, const char *s);
/* Append with &, <, >, " escaped for HTML. */
int  http_body_append_html(http_response_t *r, const char *s);
void http_body_set(http_response_t *r, const char *s);

#endif /* HTTP_RESPONSE_H */
