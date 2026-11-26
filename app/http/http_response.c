/*
 * HTTP/1.1 response header builder.
 */
#include <stdio.h>
#include <string.h>

#include "http_response.h"

const char *http_status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default:  return "Unknown";
    }
}

size_t http_response_header(const http_response_t *r, char *out, size_t size)
{
    int n = snprintf(out, size,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n"
                     "%s%s%s"
                     "\r\n",
                     r->status, http_status_text(r->status),
                     (r->content_type != NULL) ? r->content_type : HTTP_CT_TEXT,
                     (unsigned)r->body_len,
                     (r->allow != NULL) ? "Allow: " : "",
                     (r->allow != NULL) ? r->allow : "",
                     (r->allow != NULL) ? "\r\n" : "");
    if (n < 0 || (size_t)n >= size) {
        return 0U;
    }
    return (size_t)n;
}

static int put(http_response_t *r, char c)
{
    if (r->body_len + 1U >= r->body_size) {
        return 0;
    }
    r->body[r->body_len++] = c;
    r->body[r->body_len] = '\0';
    return 1;
}

int http_body_append(http_response_t *r, const char *s)
{
    while (*s != '\0') {
        if (!put(r, *s++)) {
            return 0;
        }
    }
    return 1;
}

int http_body_append_html(http_response_t *r, const char *s)
{
    for (; *s != '\0'; s++) {
        int ok;
        switch (*s) {
        case '&': ok = http_body_append(r, "&amp;");  break;
        case '<': ok = http_body_append(r, "&lt;");   break;
        case '>': ok = http_body_append(r, "&gt;");   break;
        case '"': ok = http_body_append(r, "&quot;"); break;
        default:  ok = put(r, *s);                     break;
        }
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

void http_body_set(http_response_t *r, const char *s)
{
    r->body_len = 0U;
    if (r->body_size > 0U) {
        r->body[0] = '\0';
    }
    (void)http_body_append(r, s);
}
