/*
 * Host tests: OTA URL / Range request / response head parsing, and the HTTP
 * POST /ota and GET /ota routes (http_router.c built with -DAPP_DEMO_OTA=1)
 * against fake "ota" and "ota_url" registry entries.
 */
#include <stdint.h>

#include "http_response.h"
#include "http_router.h"
#include "ota_http_msg.h"
#include "registry.h"
#include "test_util.h"

/* ---------------------------------------------------------------- URL */
static void test_url(void)
{
    ota_url_t u;
    CHECK(ota_url_parse("http://192.168.1.10:8080/fw/eco.bin", &u));
    CHECK_EQ_STR(u.host, "192.168.1.10");
    CHECK_EQ_INT(u.port, 8080);
    CHECK_EQ_STR(u.path, "/fw/eco.bin");
    CHECK(ota_url_parse("http://10.0.0.1/a", &u));
    CHECK_EQ_INT(u.port, 80);
    CHECK(ota_url_parse("http://255.255.255.255:65535/", &u));
    CHECK_EQ_STR(u.path, "/");

    CHECK(!ota_url_parse("https://192.168.1.10/fw.bin", &u));       /* no TLS */
    CHECK(!ota_url_parse("http://broker.local/fw.bin", &u));        /* no DNS */
    CHECK(!ota_url_parse("http://192.168.1/fw.bin", &u));
    CHECK(!ota_url_parse("http://192.168.1.256/fw.bin", &u));
    CHECK(!ota_url_parse("http://192.168.1.1.1/fw.bin", &u));
    CHECK(!ota_url_parse("http://1922.168.1.1/fw.bin", &u));
    CHECK(!ota_url_parse("http://192.168.1.10", &u));               /* no path */
    CHECK(!ota_url_parse("http://192.168.1.10:0/x", &u));
    CHECK(!ota_url_parse("http://192.168.1.10:65536/x", &u));
    CHECK(!ota_url_parse("http://192.168.1.10:80a/x", &u));
    CHECK(!ota_url_parse("http://192.168.1.10:/x", &u));
    CHECK(!ota_url_parse("http://192.168.1.10/a b", &u));           /* space in path */
    CHECK(!ota_url_parse(NULL, &u));
    char longurl[OTA_URL_MAX + 40];
    memset(longurl, 'a', sizeof longurl);
    memcpy(longurl, "http://1.2.3.4/", 15);
    longurl[sizeof longurl - 1U] = '\0';
    CHECK(!ota_url_parse(longurl, &u));
}

static void test_range_request(void)
{
    ota_url_t u;
    char buf[160];
    (void)ota_url_parse("http://192.168.1.10:8080/fw.bin", &u);
    size_t n = ota_http_range_request(buf, sizeof buf, &u, 1024U, 2047U);
    CHECK(n > 0U);
    CHECK_EQ_STR(buf, "GET /fw.bin HTTP/1.1\r\nHost: 192.168.1.10:8080\r\n"
                      "Range: bytes=1024-2047\r\nConnection: close\r\n\r\n");
    CHECK_EQ_INT(n, strlen(buf));
    CHECK_EQ_INT(ota_http_range_request(buf, 20U, &u, 0U, 1U), 0);  /* too small */
}

/* ---------------------------------------------------------------- response head */
static ota_head_status_t head(const char *s, ota_http_head_t *h)
{
    return ota_http_parse_head(s, strlen(s), h);
}

static void test_head(void)
{
    ota_http_head_t h;
    const char *r206 = "HTTP/1.1 206 Partial Content\r\nContent-Type: application/octet-stream\r\n"
                       "content-range: bytes 1024-2047/40960\r\nContent-Length: 1024\r\n\r\nBODY";
    CHECK_EQ_INT(head(r206, &h), OTA_HEAD_OK);
    CHECK_EQ_INT(h.status, 206);
    CHECK(h.has_range);
    CHECK_EQ_INT(h.range_first, 1024);
    CHECK_EQ_INT(h.range_last, 2047);
    CHECK_EQ_INT(h.range_total, 40960);
    CHECK(h.has_length);
    CHECK_EQ_INT(h.content_length, 1024);
    CHECK_EQ_INT(h.head_len, strlen(r206) - 4U);                    /* "BODY" follows */

    CHECK_EQ_INT(head("HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\n", &h), OTA_HEAD_OK);
    CHECK_EQ_INT(h.status, 200);
    CHECK(!h.has_range);
    CHECK_EQ_INT(h.content_length, 5);
    CHECK_EQ_INT(head("HTTP/1.1 404 Not Found\r\n\r\n", &h), OTA_HEAD_OK);
    CHECK_EQ_INT(h.status, 404);
    CHECK(!h.has_length);
    CHECK_EQ_INT(head("HTTP/1.1 200\r\n\r\n", &h), OTA_HEAD_OK);    /* empty reason */

    CHECK_EQ_INT(head("HTTP/1.1 206 Partial", &h), OTA_HEAD_INCOMPLETE);
    CHECK_EQ_INT(head("HTTP/1.1 206 X\r\nContent-Length: 4\r\n", &h), OTA_HEAD_INCOMPLETE);
    CHECK_EQ_INT(head("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 206 X\r\nContent-Range: bytes 0-9/*\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 206 X\r\nContent-Range: bytes 9-0/100\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 206 X\r\nContent-Range: bytes 0-100/100\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 206 X\r\nContent-Range: items 0-1/2\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 200 OK\r\nContent-Length: 12x\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 200 OK\r\nContent-Length: 99999999999\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/2 200 OK\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 20 OK\r\n\r\n", &h), OTA_HEAD_BAD);
    CHECK_EQ_INT(head("HTTP/1.1 2000 OK\r\n\r\n", &h), OTA_HEAD_BAD);

    char big[OTA_HEAD_MAX + 8];
    memset(big, 'x', sizeof big);
    CHECK_EQ_INT(ota_http_parse_head(big, sizeof big, &h), OTA_HEAD_BAD);  /* never ends */
}

/* ---------------------------------------------------------------- /ota route */
static char last_ota[64];
static char last_url[OTA_URL_MAX];

static reg_status_t get_ota(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_str(w, "state", "idle");
    return REG_OK;
}

static reg_status_t set_ota(const char *v, void *ctx)
{
    (void)ctx;
    if (strncmp(v, "start ", 6) != 0 && strcmp(v, "abort") != 0 && strcmp(v, "reset") != 0) {
        return REG_ERR_VALUE;
    }
    (void)snprintf(last_ota, sizeof last_ota, "%s", v);
    return REG_OK;
}

static reg_status_t get_url(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_str(w, "ota_url", last_url);
    return REG_OK;
}

static reg_status_t set_url(const char *v, void *ctx)
{
    (void)ctx;
    ota_url_t u;
    if (!ota_url_parse(v, &u)) {
        return REG_ERR_VALUE;
    }
    (void)snprintf(last_url, sizeof last_url, "%s", v);
    return REG_OK;
}

static const reg_entry_t entries[] = {
    { "ota", NULL, get_ota, set_ota, NULL },
    { "ota_url", NULL, get_url, set_url, NULL },
};

static char body[1536];
static char raw[1024];

static int post(const char *json, http_response_t *resp)
{
    http_request_t req;
    (void)snprintf(raw, sizeof raw, "POST /ota HTTP/1.1\r\nContent-Length: %u\r\n\r\n%s",
                   (unsigned)strlen(json), json);
    resp->body = body;
    resp->body_size = sizeof body;
    if (http_parse_request(raw, strlen(raw), &req) != HTTP_PARSE_OK) {
        return -1;
    }
    http_route(&req, resp);
    return resp->status;
}

static void test_ota_route(void)
{
    http_response_t resp;
    http_request_t req;
    registry_clear();
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++) {
        (void)registry_register(&entries[i]);
    }
    last_ota[0] = '\0';
    last_url[0] = '\0';

    CHECK_EQ_INT(post("{\"url\":\"http://192.168.1.10:8080/fw.bin\",\"crc32\":\"1a2b3c4d\"}", &resp), 200);
    CHECK_EQ_STR(last_url, "http://192.168.1.10:8080/fw.bin");
    CHECK_EQ_STR(last_ota, "start 1a2b3c4d");
    CHECK_EQ_STR(body, "{\"state\":\"idle\"}");

    CHECK_EQ_INT(post("{\"crc32\":\"deadbeef\"}", &resp), 200);          /* url kept */
    CHECK_EQ_STR(last_ota, "start deadbeef");
    CHECK_EQ_INT(post("{\"cmd\":\"abort\"}", &resp), 200);
    CHECK_EQ_STR(last_ota, "abort");
    CHECK_EQ_INT(post("{\"cmd\":\"reset\"}", &resp), 200);
    CHECK_EQ_STR(last_ota, "reset");

    CHECK_EQ_INT(post("{\"cmd\":\"reboot\"}", &resp), 400);              /* setter refuses */
    CHECK_EQ_INT(post("{\"url\":\"http://x/y\",\"crc32\":\"1\"}", &resp), 400);
    CHECK_EQ_STR(last_url, "http://192.168.1.10:8080/fw.bin");          /* unchanged */
    CHECK_EQ_INT(post("{\"url\":\"http://1.2.3.4/a\"}", &resp), 400);    /* crc32 missing */
    CHECK(strstr(body, "crc32") != NULL);
    CHECK_EQ_STR(last_url, "http://192.168.1.10:8080/fw.bin");          /* not changed either */
    CHECK_EQ_INT(post("{\"crc32\":12}", &resp), 400);                    /* wrong type */
    CHECK_EQ_INT(post("{bad", &resp), 400);

    const char *get = "GET /ota HTTP/1.1\r\n\r\n";
    resp.body = body;
    resp.body_size = sizeof body;
    CHECK_EQ_INT(http_parse_request(get, strlen(get), &req), HTTP_PARSE_OK);
    http_route(&req, &resp);
    CHECK_EQ_INT(resp.status, 200);
    CHECK_EQ_STR(body, "{\"state\":\"idle\"}");
}

int main(void)
{
    RUN(test_url);
    RUN(test_range_request);
    RUN(test_head);
    RUN(test_ota_route);
    TEST_SUMMARY("test_ota_http");
}
