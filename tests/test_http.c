/*
 * Host tests: HTTP request parser, router and response header, against fake
 * registry entries (the real ones live in app/registry/commands.c).
 */
#include <stdint.h>

#include "http_response.h"
#include "http_router.h"
#include "registry.h"
#include "test_util.h"

/* ---------------------------------------------------------------- fake registry */

static uint32_t interval = 2000;
static int have_reading = 1;

static reg_status_t get_temp(json_writer_t *w, void *ctx)
{
    (void)ctx;
    if (!have_reading) {
        return REG_ERR_NO_DATA;
    }
    jw_kv_uint(w, "node", 1);
    jw_kv_fixed2(w, "value", 2510);
    jw_kv_uint(w, "timestamp", 1234);
    jw_kv_uint(w, "seq", 12);
    return REG_OK;
}

static reg_status_t get_interval(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_uint(w, "interval_ms", interval);
    return REG_OK;
}

static reg_status_t set_interval(const char *v, void *ctx)
{
    (void)ctx;
    uint32_t ms;
    if (!reg_parse_u32(v, 100, 60000, &ms)) {
        return REG_ERR_VALUE;
    }
    interval = ms;
    return REG_OK;
}

static reg_status_t get_status(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_str(w, "note", "<b>&");
    return REG_OK;
}

static const reg_entry_t entries[] = {
    { "temp", NULL, get_temp, NULL, NULL },
    { "interval", NULL, get_interval, set_interval, NULL },
    { "status", NULL, get_status, NULL, NULL },
};

static void setup(void)
{
    registry_clear();
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++) {
        (void)registry_register(&entries[i]);
    }
    interval = 2000;
    have_reading = 1;
}

/* ---------------------------------------------------------------- helpers */

static char body[1536];

static http_parse_t parse(const char *s, http_request_t *req)
{
    return http_parse_request(s, strlen(s), req);
}

static int route(const char *raw, http_response_t *resp)
{
    http_request_t req;
    resp->body = body;
    resp->body_size = sizeof body;
    if (parse(raw, &req) != HTTP_PARSE_OK) {
        return -1;
    }
    http_route(&req, resp);
    return resp->status;
}

/* ---------------------------------------------------------------- parser */

static void parse_get(void)
{
    http_request_t r;
    CHECK_EQ_INT(parse("GET /temp HTTP/1.1\r\nHost: 192.168.1.50\r\nAccept: */*\r\n\r\n", &r), HTTP_PARSE_OK);
    CHECK_EQ_INT(r.method, HTTP_GET);
    CHECK_EQ_STR(r.path, "/temp");
    CHECK(r.query == NULL);
    CHECK_EQ_INT(r.body_len, 0);
    CHECK_EQ_INT(parse("GET / HTTP/1.0\r\n\r\n", &r), HTTP_PARSE_OK);
    CHECK_EQ_STR(r.path, "/");
}

static void parse_query(void)
{
    http_request_t r;
    CHECK_EQ_INT(parse("GET /log?since_seq=10 HTTP/1.1\r\n\r\n", &r), HTTP_PARSE_OK);
    CHECK_EQ_STR(r.path, "/log");
    CHECK_EQ_INT(r.query_len, strlen("since_seq=10"));
    CHECK(memcmp(r.query, "since_seq=10", r.query_len) == 0);
}

static void parse_post_body(void)
{
    http_request_t r;
    const char *raw = "POST /interval HTTP/1.1\r\nContent-Type: application/json\r\n"
                      "content-length: 20\r\n\r\n{\"interval_ms\":5000}";
    CHECK_EQ_INT(parse(raw, &r), HTTP_PARSE_OK);
    CHECK_EQ_INT(r.method, HTTP_POST);
    CHECK_EQ_INT(r.body_len, 20);
    CHECK(memcmp(r.body, "{\"interval_ms\":5000}", 20) == 0);
    CHECK_EQ_INT(r.total_len, strlen(raw));
}

static void parse_incomplete(void)
{
    http_request_t r;
    CHECK_EQ_INT(parse("GET /temp HTTP/1.1\r\nHost: x\r\n", &r), HTTP_PARSE_INCOMPLETE);
    CHECK_EQ_INT(parse("POST /interval HTTP/1.1\r\nContent-Length: 20\r\n\r\n{\"interval", &r),
                 HTTP_PARSE_INCOMPLETE);
    CHECK_EQ_INT(parse("", &r), HTTP_PARSE_INCOMPLETE);
}

static void parse_bad(void)
{
    http_request_t r;
    CHECK_EQ_INT(parse("GET\r\n\r\n", &r), HTTP_PARSE_BAD);
    CHECK_EQ_INT(parse("GET temp HTTP/1.1\r\n\r\n", &r), HTTP_PARSE_BAD);          /* no leading / */
    CHECK_EQ_INT(parse("GET /temp HTTP/2.0\r\n\r\n", &r), HTTP_PARSE_BAD);
    CHECK_EQ_INT(parse("GET /temp\r\n\r\n", &r), HTTP_PARSE_BAD);
    CHECK_EQ_INT(parse("LONGMETHOD /x HTTP/1.1\r\n\r\n", &r), HTTP_PARSE_BAD);
    CHECK_EQ_INT(parse("POST /x HTTP/1.1\r\nContent-Length: 1x\r\n\r\n", &r), HTTP_PARSE_BAD);
    CHECK_EQ_INT(parse("POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", &r), HTTP_PARSE_BAD);
    char longpath[200];
    snprintf(longpath, sizeof longpath, "GET /%0100d HTTP/1.1\r\n\r\n", 0);
    CHECK_EQ_INT(parse(longpath, &r), HTTP_PARSE_BAD);
}

static void parse_too_large(void)
{
    http_request_t r;
    CHECK_EQ_INT(parse("POST /interval HTTP/1.1\r\nContent-Length: 100000\r\n\r\n", &r), HTTP_PARSE_TOO_LARGE);
    static char big[HTTP_REQUEST_MAX + 10];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    CHECK_EQ_INT(parse(big, &r), HTTP_PARSE_TOO_LARGE);                            /* never ends headers */
}

/* ---------------------------------------------------------------- router */

static void route_temp(void)
{
    http_response_t resp;
    setup();
    CHECK_EQ_INT(route("GET /temp HTTP/1.1\r\n\r\n", &resp), 200);
    CHECK_EQ_STR(resp.content_type, HTTP_CT_JSON);
    CHECK_EQ_STR(body, "{\"node\":1,\"value\":25.10,\"timestamp\":1234,\"seq\":12}");
    CHECK_EQ_INT(resp.body_len, strlen(body));
    have_reading = 0;
    CHECK_EQ_INT(route("GET /temp HTTP/1.1\r\n\r\n", &resp), 503);
    CHECK(strstr(body, "\"error\":\"no data\"") != NULL);
}

static void route_interval(void)
{
    http_response_t resp;
    setup();
    CHECK_EQ_INT(route("GET /interval HTTP/1.1\r\n\r\n", &resp), 200);
    CHECK_EQ_STR(body, "{\"interval_ms\":2000}");
    CHECK_EQ_INT(route("POST /interval HTTP/1.1\r\nContent-Length: 20\r\n\r\n{\"interval_ms\":5000}", &resp), 200);
    CHECK_EQ_STR(body, "{\"interval_ms\":5000}");
    CHECK_EQ_INT(interval, 5000);
}

/* POST /interval with a correct Content-Length for the given body */
static int post_interval(const char *json, http_response_t *resp)
{
    static char raw[512];
    snprintf(raw, sizeof raw, "POST /interval HTTP/1.1\r\nContent-Length: %u\r\n\r\n%s",
             (unsigned)strlen(json), json);
    return route(raw, resp);
}

static void route_interval_errors(void)
{
    http_response_t resp;
    setup();
    CHECK_EQ_INT(post_interval("hello", &resp), 400);
    CHECK_EQ_STR(body, "{\"error\":\"bad json\",\"detail\":\"syntax\"}");
    CHECK_EQ_INT(post_interval("{\"x\":10}", &resp), 400);
    CHECK_EQ_STR(body, "{\"error\":\"interval_ms\",\"detail\":\"missing\"}");
    CHECK_EQ_INT(post_interval("{\"interval_ms\":\"fast\"}", &resp), 400);
    CHECK_EQ_STR(body, "{\"error\":\"interval_ms\",\"detail\":\"wrong type\"}");
    CHECK_EQ_INT(post_interval("{\"interval_ms\":2.5}", &resp), 400);
    CHECK_EQ_STR(body, "{\"error\":\"interval_ms\",\"detail\":\"wrong type\"}");
    CHECK_EQ_INT(post_interval("{\"interval_ms\":50}", &resp), 400);
    CHECK_EQ_STR(body, "{\"error\":\"bad value\",\"key\":\"interval\"}");
    CHECK_EQ_INT(post_interval("{\"interval_ms\":-5}", &resp), 400);
    CHECK_EQ_STR(body, "{\"error\":\"interval_ms\",\"detail\":\"out of range\"}");
    CHECK_EQ_INT(post_interval("{\"interval_ms\":99999999999}", &resp), 400);
    CHECK_EQ_INT(route("POST /interval HTTP/1.1\r\n\r\n", &resp), 400);                  /* empty body */
    CHECK_EQ_INT(interval, 2000);
    CHECK_EQ_INT(post_interval(" {\"interval_ms\" : 60000} ", &resp), 200);
    CHECK_EQ_INT(interval, 60000);
}

static void route_errors(void)
{
    http_response_t resp;
    setup();
    CHECK_EQ_INT(route("GET /nope HTTP/1.1\r\n\r\n", &resp), 404);
    CHECK_EQ_STR(body, "{\"error\":\"not found\",\"detail\":\"/nope\"}");
    CHECK_EQ_INT(route("DELETE /temp HTTP/1.1\r\n\r\n", &resp), 405);
    CHECK_EQ_STR(resp.allow, "GET");
    CHECK_EQ_INT(route("POST /temp HTTP/1.1\r\n\r\n", &resp), 405);
    CHECK_EQ_INT(route("PUT /interval HTTP/1.1\r\n\r\n", &resp), 405);
    CHECK_EQ_STR(resp.allow, "GET, POST");
    CHECK_EQ_INT(route("GET /version HTTP/1.1\r\n\r\n", &resp), 404);                 /* not registered here */
}

static void route_root_page(void)
{
    http_response_t resp;
    setup();
    CHECK_EQ_INT(route("GET / HTTP/1.1\r\n\r\n", &resp), 200);
    CHECK_EQ_STR(resp.content_type, HTTP_CT_HTML);
    CHECK(strstr(body, "<h1>Eco-Connect gateway</h1>") != NULL);
    CHECK(strstr(body, "{&quot;node&quot;:1") != NULL);                               /* JSON escaped */
    CHECK(strstr(body, "&lt;b&gt;&amp;") != NULL);
    CHECK(strstr(body, "</html>") != NULL);                                           /* fits in the body buffer */
}

static void response_header(void)
{
    char hdr[256];
    http_response_t r = { .status = 405, .content_type = HTTP_CT_JSON, .allow = "GET", .body = NULL, .body_size = 0, .body_len = 42 };
    size_t n = http_response_header(&r, hdr, sizeof hdr);
    CHECK(n > 0U);
    CHECK_EQ_STR(hdr, "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: application/json\r\n"
                      "Content-Length: 42\r\nCache-Control: no-store\r\nConnection: close\r\n"
                      "Allow: GET\r\n\r\n");
    r.allow = NULL;
    r.status = 200;
    (void)http_response_header(&r, hdr, sizeof hdr);
    CHECK(strncmp(hdr, "HTTP/1.1 200 OK\r\n", 17) == 0);
    CHECK(strstr(hdr, "Allow") == NULL);
    CHECK_EQ_INT(http_response_header(&r, hdr, 20), 0);
}

int main(void)
{
    RUN(parse_get);
    RUN(parse_query);
    RUN(parse_post_body);
    RUN(parse_incomplete);
    RUN(parse_bad);
    RUN(parse_too_large);
    RUN(route_temp);
    RUN(route_interval);
    RUN(route_interval_errors);
    RUN(route_errors);
    RUN(route_root_page);
    RUN(response_header);
    TEST_SUMMARY("test_http");
}
