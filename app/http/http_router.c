/*
 * HTTP request parser and router.
 */
#include <stdio.h>
#include <string.h>

#include "http_router.h"
#include "json_parser.h"
#include "json_writer.h"
#include "registry.h"

/* ---------------------------------------------------------------- parser */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* case-insensitive "starts with" for header names */
static int header_is(const char *line, size_t len, const char *name)
{
    size_t n = strlen(name);
    if (len < n) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (lower(line[i]) != name[i]) {
            return 0;
        }
    }
    return 1;
}

static const char *find_crlfcrlf(const char *buf, size_t len)
{
    for (size_t i = 0; i + 3U < len; i++) {
        if (buf[i] == '\r' && buf[i + 1U] == '\n' && buf[i + 2U] == '\r' && buf[i + 3U] == '\n') {
            return &buf[i];
        }
    }
    return NULL;
}

http_parse_t http_parse_request(const char *buf, size_t len, http_request_t *req)
{
    memset(req, 0, sizeof *req);

    const char *hdr_end = find_crlfcrlf(buf, len);
    if (hdr_end == NULL) {
        return (len >= HTTP_REQUEST_MAX) ? HTTP_PARSE_TOO_LARGE : HTTP_PARSE_INCOMPLETE;
    }
    const char *end = hdr_end + 2;              /* end of the last header line incl. CRLF */
    const char *p = buf;

    /* request line: METHOD SP TARGET SP HTTP/1.x CRLF */
    const char *sp1 = memchr(p, ' ', (size_t)(end - p));
    if (sp1 == NULL || sp1 == p || (size_t)(sp1 - p) >= sizeof req->method_text) {
        return HTTP_PARSE_BAD;
    }
    memcpy(req->method_text, p, (size_t)(sp1 - p));
    req->method_text[sp1 - p] = '\0';
    if (strcmp(req->method_text, "GET") == 0) {
        req->method = HTTP_GET;
    } else if (strcmp(req->method_text, "POST") == 0) {
        req->method = HTTP_POST;
    } else {
        req->method = HTTP_OTHER;
    }

    const char *target = sp1 + 1;
    const char *sp2 = memchr(target, ' ', (size_t)(end - target));
    const char *eol = memchr(target, '\r', (size_t)(end - target));
    if (sp2 == NULL || eol == NULL || sp2 > eol || *target != '/') {
        return HTTP_PARSE_BAD;
    }
    if ((size_t)(eol - (sp2 + 1)) != 8U || memcmp(sp2 + 1, "HTTP/1.", 7) != 0 ||
        (sp2[8] != '0' && sp2[8] != '1')) {
        return HTTP_PARSE_BAD;
    }
    const char *q = memchr(target, '?', (size_t)(sp2 - target));
    const char *path_end = (q != NULL) ? q : sp2;
    if ((size_t)(path_end - target) >= sizeof req->path) {
        return HTTP_PARSE_BAD;
    }
    memcpy(req->path, target, (size_t)(path_end - target));
    req->path[path_end - target] = '\0';
    if (q != NULL) {
        req->query = q + 1;
        req->query_len = (size_t)(sp2 - (q + 1));
    }

    /* headers: only Content-Length matters; chunked bodies are not supported */
    size_t content_length = 0U;
    p = eol + 2;
    while (p < end) {
        const char *le = memchr(p, '\r', (size_t)(end - p));
        if (le == NULL || le[1] != '\n') {          /* le[1] is inside the final CRLFCRLF */
            return HTTP_PARSE_BAD;
        }
        size_t ll = (size_t)(le - p);
        if (header_is(p, ll, "content-length:")) {
            const char *v = p + 15;
            while (v < le && (*v == ' ' || *v == '\t')) {
                v++;
            }
            if (v == le) {
                return HTTP_PARSE_BAD;
            }
            content_length = 0U;
            for (; v < le; v++) {
                if (*v < '0' || *v > '9') {
                    return HTTP_PARSE_BAD;
                }
                content_length = content_length * 10U + (size_t)(*v - '0');
                if (content_length > HTTP_BODY_MAX) {
                    return HTTP_PARSE_TOO_LARGE;
                }
            }
        } else if (header_is(p, ll, "transfer-encoding:")) {
            return HTTP_PARSE_BAD;
        }
        p = le + 2;
    }

    const char *body = hdr_end + 4;
    size_t have = len - (size_t)(body - buf);
    if (have < content_length) {
        return HTTP_PARSE_INCOMPLETE;
    }
    req->body = body;
    req->body_len = content_length;
    req->total_len = (size_t)(body - buf) + content_length;
    return HTTP_PARSE_OK;
}

/* ---------------------------------------------------------------- responses */

static void json_error(http_response_t *resp, int status, const char *msg, const char *detail)
{
    json_writer_t w;
    resp->status = status;
    resp->content_type = HTTP_CT_JSON;
    jw_init(&w, resp->body, resp->body_size);
    jw_object_begin(&w);
    jw_kv_str(&w, "error", msg);
    if (detail != NULL) {
        jw_kv_str(&w, "detail", detail);
    }
    jw_object_end(&w);
    resp->body_len = jw_finish(&w);
}

void http_error_response(int status, const char *msg, http_response_t *resp)
{
    resp->allow = NULL;
    json_error(resp, status, msg, NULL);
}

static int reg_to_http(reg_status_t st)
{
    switch (st) {
    case REG_OK:           return 200;
    case REG_ERR_UNKNOWN:  return 404;
    case REG_ERR_VALUE:    return 400;
    case REG_ERR_PARAM:    return 400;
    case REG_ERR_READONLY: return 405;
    case REG_ERR_NO_DATA:  return 503;
    default:               return 500;
    }
}

static void reply_registry_get(const char *key, http_response_t *resp)
{
    reg_status_t st = registry_get(key, resp->body, resp->body_size);
    resp->status = reg_to_http(st);
    resp->content_type = HTTP_CT_JSON;
    resp->body_len = strlen(resp->body);
}

/* ---------------------------------------------------------------- handlers */

static void h_temp(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    reply_registry_get("temp", resp);
}

static void h_interval_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    reply_registry_get("interval", resp);
}

/* POST /interval {"interval_ms":5000} -> registry_set("interval", "5000") */
static void h_interval_post(const http_request_t *req, http_response_t *resp)
{
    json_object_t obj;
    int32_t ms = 0;
    char value[12];

    json_status_t js = json_parse_object(req->body, req->body_len, &obj);
    if (js != JSON_OK) {
        json_error(resp, 400, "bad json", json_status_str(js));
        return;
    }
    js = json_get_int(&obj, "interval_ms", &ms);
    if (js != JSON_OK) {
        json_error(resp, 400, "interval_ms", json_status_str(js));
        return;
    }
    if (ms < 0) {
        json_error(resp, 400, "interval_ms", "out of range");
        return;
    }
    (void)snprintf(value, sizeof value, "%ld", (long)ms);
    reg_status_t st = registry_set("interval", value, resp->body, resp->body_size);
    resp->status = reg_to_http(st);
    resp->content_type = HTTP_CT_JSON;
    resp->body_len = strlen(resp->body);
}

static void h_status(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    reply_registry_get("status", resp);
}

static void h_version(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    reply_registry_get("version", resp);
}

static void page_section(http_response_t *resp, const char *title, const char *key)
{
    char tmp[320];
    (void)registry_get(key, tmp, sizeof tmp);      /* error JSON is shown as is */
    (void)http_body_append(resp, "<h2>");
    (void)http_body_append(resp, title);
    (void)http_body_append(resp, "</h2><pre>");
    (void)http_body_append_html(resp, tmp);
    (void)http_body_append(resp, "</pre>");
}

static void h_root(const http_request_t *req, http_response_t *resp)
{
    (void)req;
    resp->status = 200;
    resp->content_type = HTTP_CT_HTML;
    http_body_set(resp,
                  "<!doctype html><html><head><meta charset=\"utf-8\">"
                  "<title>Eco-Connect gateway</title></head><body>"
                  "<h1>Eco-Connect gateway</h1>");
    page_section(resp, "Latest reading", "temp");
    page_section(resp, "Status", "status");
    page_section(resp, "Version", "version");
    (void)http_body_append(resp,
                           "<h2>API</h2><ul>"
                           "<li><a href=\"/temp\">GET /temp</a></li>"
                           "<li><a href=\"/interval\">GET /interval</a></li>"
                           "<li>POST /interval {\"interval_ms\":2000}</li>"
                           "<li><a href=\"/status\">GET /status</a></li>"
                           "<li><a href=\"/version\">GET /version</a></li>"
                           "<li><a href=\"/log\">GET /log</a></li>"
                           "</ul></body></html>");
}

typedef void (*http_handler_t)(const http_request_t *req, http_response_t *resp);

typedef struct {
    const char    *path;
    http_handler_t get;
    http_handler_t post;
    const char    *allow;
} route_t;

static const route_t routes[] = {
    { "/",         h_root,         NULL,            "GET" },
    { "/temp",     h_temp,         NULL,            "GET" },
    { "/interval", h_interval_get, h_interval_post, "GET, POST" },
    { "/status",   h_status,       NULL,            "GET" },
    { "/version",  h_version,      NULL,            "GET" },
};

void http_route(const http_request_t *req, http_response_t *resp)
{
    resp->allow = NULL;
    resp->body_len = 0U;
    if (resp->body_size > 0U) {
        resp->body[0] = '\0';
    }

    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++) {
        const route_t *r = &routes[i];
        if (strcmp(req->path, r->path) != 0) {
            continue;
        }
        http_handler_t h = (req->method == HTTP_GET) ? r->get : (req->method == HTTP_POST) ? r->post : NULL;
        if (h == NULL) {
            json_error(resp, 405, "method not allowed", req->method_text);
            resp->allow = r->allow;
            return;
        }
        h(req, resp);
        return;
    }
    json_error(resp, 404, "not found", req->path);
}
