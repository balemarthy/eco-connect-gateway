/*
 * HTTP server task: accept, read one request (bounded), route, answer, close.
 *
 * All buffers are static and sized by HTTP_REQUEST_MAX / HTTP_RESPONSE_BODY_MAX.
 * lwIP itself takes the PCB, netconn and pbufs from its static pools; the
 * FreeRTOS port creates each connection's mailbox from heap_4 (returned when
 * the connection is deleted).
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/api.h"

#define LOG_TAG LOG_MOD_HTTP
#include "log.h"

#include "http_response.h"
#include "http_router.h"
#include "http_server.h"
#include "net_init.h"

#define HTTP_STACK_WORDS        1024U
#define HTTP_PRIORITY           (tskIDLE_PRIORITY + 2U)
#define HTTP_RESPONSE_BODY_MAX  1536U
#define HTTP_HEADER_MAX         256U

static StaticTask_t task_tcb;
static StackType_t  task_stack[HTTP_STACK_WORDS];

static char req_buf[HTTP_REQUEST_MAX];
static char body_buf[HTTP_RESPONSE_BODY_MAX];
static char hdr_buf[HTTP_HEADER_MAX];

/* Reads until a full request is parsed, the buffer is full, or the peer stops. */
static http_parse_t read_request(struct netconn *c, http_request_t *req, bool *got_any)
{
    size_t len = 0U;
    http_parse_t pr = HTTP_PARSE_INCOMPLETE;

    *got_any = false;
    while (pr == HTTP_PARSE_INCOMPLETE) {
        struct netbuf *nb = NULL;
        if (netconn_recv(c, &nb) != ERR_OK) {
            return HTTP_PARSE_INCOMPLETE;            /* timeout or closed */
        }
        *got_any = true;
        do {
            void *data;
            u16_t n;
            netbuf_data(nb, &data, &n);
            size_t room = sizeof req_buf - len;
            size_t take = (n < room) ? n : room;
            memcpy(&req_buf[len], data, take);
            len += take;
        } while (netbuf_next(nb) >= 0);
        netbuf_delete(nb);

        pr = http_parse_request(req_buf, len, req);
        if (pr == HTTP_PARSE_INCOMPLETE && len >= sizeof req_buf) {
            pr = HTTP_PARSE_TOO_LARGE;
        }
    }
    return pr;
}

static void serve(struct netconn *c)
{
    http_request_t  req;
    http_response_t resp = { .body = body_buf, .body_size = sizeof body_buf };
    bool got_any;

    netconn_set_recvtimeout(c, HTTP_RECV_TIMEOUT_MS);
    http_parse_t pr = read_request(c, &req, &got_any);

    switch (pr) {
    case HTTP_PARSE_OK:
        http_route(&req, &resp);
        break;
    case HTTP_PARSE_TOO_LARGE:
        http_error_response(413, "request too large", &resp);
        break;
    case HTTP_PARSE_BAD:
        http_error_response(400, "bad request", &resp);
        break;
    default:
        if (!got_any) {
            return;                                  /* connected and said nothing */
        }
        http_error_response(408, "request timeout", &resp);
        break;
    }

    size_t hlen = http_response_header(&resp, hdr_buf, sizeof hdr_buf);
    if (hlen > 0U && netconn_write(c, hdr_buf, hlen, NETCONN_COPY) == ERR_OK && resp.body_len > 0U) {
        (void)netconn_write(c, resp.body, resp.body_len, NETCONN_COPY);
    }
    if (pr == HTTP_PARSE_OK) {
        LOG_I("%s %s %d", req.method_text, req.path, resp.status);
    } else {
        LOG_W("rejected request: %d", resp.status);
    }
}

static void http_task(void *arg)
{
    (void)arg;
    while (!net_stack_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    struct netconn *listener = netconn_new(NETCONN_TCP);
    if (listener == NULL || netconn_bind(listener, IP_ADDR_ANY, HTTP_PORT) != ERR_OK ||
        netconn_listen(listener) != ERR_OK) {
        LOG_E("cannot listen on :%u", (unsigned)HTTP_PORT);
        vTaskSuspend(NULL);
    }
    LOG_I("listening :%u", (unsigned)HTTP_PORT);

    for (;;) {
        struct netconn *c = NULL;
        if (netconn_accept(listener, &c) == ERR_OK) {
            serve(c);
            (void)netconn_close(c);
            (void)netconn_delete(c);
        }
    }
}

void http_server_start(void)
{
    TaskHandle_t h = xTaskCreateStatic(http_task, "http", HTTP_STACK_WORDS, NULL, HTTP_PRIORITY,
                                       task_stack, &task_tcb);
    configASSERT(h != NULL);
}
