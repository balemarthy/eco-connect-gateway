/*
 * HTTP request parser and router. Pure C (uses the registry and the JSON modules,
 * no lwIP), so it runs in the host tests with fake registry entries.
 *
 *   GET  /           HTML status page
 *   GET  /temp       latest reading            {"node":1,"value":25.10,"timestamp":..,"seq":..}
 *   GET  /interval   sampling period           {"interval_ms":2000}
 *   POST /interval   body {"interval_ms":5000} -> {"interval_ms":5000}   (the "SET /interval" of the brief)
 *   GET  /status     counters, link, heap
 *   GET  /version    versions
 *   GET  /log        last stored readings (step 16, when storage registers "log")
 * Errors are JSON: 400 bad request/body, 404 unknown path, 405 wrong method,
 * 413 body too large, 503 no data yet.
 */
#ifndef HTTP_ROUTER_H
#define HTTP_ROUTER_H

#include <stddef.h>
#include <stdint.h>

#include "http_response.h"

#define HTTP_PATH_MAX      64U
#define HTTP_BODY_MAX      512U
#define HTTP_REQUEST_MAX   1024U      /* request line + headers + body */

typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_OTHER
} http_method_t;

typedef enum {
    HTTP_PARSE_OK = 0,
    HTTP_PARSE_INCOMPLETE,     /* need more bytes */
    HTTP_PARSE_BAD,            /* malformed: answer 400 */
    HTTP_PARSE_TOO_LARGE       /* body or headers over the limits: answer 413 */
} http_parse_t;

typedef struct {
    http_method_t method;
    char          method_text[8];
    char          path[HTTP_PATH_MAX];       /* without the query string */
    const char   *query;                     /* points into the input, NULL if none */
    size_t        query_len;
    const char   *body;                      /* points into the input */
    size_t        body_len;
    size_t        total_len;                 /* bytes of the input this request used */
} http_request_t;

http_parse_t http_parse_request(const char *buf, size_t len, http_request_t *req);

/* Fills resp (status, content type, body). resp->body/body_size must be set. */
void http_route(const http_request_t *req, http_response_t *resp);

/* Error response for a parse failure (400 / 413 / 408). */
void http_error_response(int status, const char *msg, http_response_t *resp);

#endif /* HTTP_ROUTER_H */
