/*
 * Protocol task.
 *
 * Reads the link byte stream, feeds the frame decoder, decodes the inner
 * packet and answers with one frame per request:
 *   PING              -> RSP S "pong"
 *   GET <key>         -> RSP J <registry result>     e.g. GET temp, GET interval
 *   GET / GET help    -> RSP J {"keys":[...]}
 *   SET <key> <value> -> RSP J <new value>           e.g. SET interval 5000
 *   errors            -> ERR J {"error":"...","key":"..."}
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_PROTO
#include "log.h"

#include "json_writer.h"
#include "proto_task.h"
#include "registry.h"
#include "serial_protocol.h"
#include "supervisor.h"
#include "uart_link.h"

#define PROTO_STACK_WORDS  768U
#define PROTO_PRIORITY     (tskIDLE_PRIORITY + 2U)
#define PROTO_TX_TIMEOUT   100U

static StaticTask_t task_tcb;
static StackType_t  task_stack[PROTO_STACK_WORDS];

static proto_rx_t     rx;
static proto_packet_t req;
static proto_packet_t rsp;
static uint8_t        tx_frame[PROTO_FRAME_MAX];
static proto_stats_t  stats;
static sup_id_t       sup_id = SUP_INVALID;

static void error_packet(proto_packet_t *out, const char *msg)
{
    char buf[PROTO_VALUE_MAX + 1U];
    json_writer_t w;
    jw_init(&w, buf, sizeof buf);
    jw_object_begin(&w);
    jw_kv_str(&w, "error", msg);
    jw_object_end(&w);
    (void)proto_packet_set(out, PROTO_OP_ERR, PROTO_T_JSON, (jw_finish(&w) > 0U) ? buf : "{}");
}

/* GET <key> | GET (list) | SET <key> <value> | PING -> registry */
static void handle(proto_packet_t *in, proto_packet_t *out)
{
    static char result[PROTO_VALUE_MAX + 1U];
    reg_status_t st;

    switch (in->op) {
    case PROTO_OP_PING:
        (void)proto_packet_set(out, PROTO_OP_RSP, PROTO_T_STR, "pong");
        return;
    case PROTO_OP_GET:
        if (in->len == 0U || strcmp(in->value, "help") == 0) {
            st = registry_list(result, sizeof result, false);   /* help text does not fit in one packet */
        } else {
            st = registry_get(in->value, result, sizeof result);
        }
        break;
    case PROTO_OP_SET: {
        char *sp = strchr(in->value, ' ');
        if (sp == NULL) {
            st = registry_set(in->value, NULL, result, sizeof result);
        } else {
            *sp = '\0';                              /* split "key value" in place */
            st = registry_set(in->value, sp + 1, result, sizeof result);
        }
        break;
    }
    default:
        error_packet(out, "unsupported op");
        return;
    }
    if (proto_packet_set(out, (st == REG_OK) ? PROTO_OP_RSP : PROTO_OP_ERR, PROTO_T_JSON, result) != PROTO_OK) {
        error_packet(out, "result too long");
    }
}

static void send_packet(const proto_packet_t *p)
{
    size_t n = proto_build_frame(p, tx_frame, sizeof tx_frame);
    if (n > 0U && !uart_link_write(tx_frame, n, PROTO_TX_TIMEOUT)) {
        LOG_W("tx timeout");
    }
}

static void on_frame(void)
{
    proto_status_t st = proto_packet_decode(rx.body, rx.len, &req);
    if (st != PROTO_OK) {
        stats.bad_packet++;
        LOG_W("bad packet: %s", proto_status_str(st));
        error_packet(&rsp, proto_status_str(st));
        send_packet(&rsp);
        return;
    }
    stats.frames_ok++;
    LOG_I("frame ok op=%s", proto_op_name(req.op));
    if (req.op == PROTO_OP_RSP || req.op == PROTO_OP_ERR) {
        return;                                    /* answers from the peer are not answered */
    }
    handle(&req, &rsp);
    if (rsp.op == PROTO_OP_RSP && req.op == PROTO_OP_GET) {
        gw_post(GW_EV_REPORT);                     /* a client got data */
    }
    send_packet(&rsp);
}

static void proto_task(void *arg)
{
    (void)arg;
    uint8_t chunk[32];

    proto_rx_init(&rx);
    LOG_I("link ready on USART1 %lu 8N1", (unsigned long)UART_LINK_BAUD);
    for (;;) {
        size_t n = uart_link_read(chunk, sizeof chunk, 1000U);
        sup_checkin(sup_id);
        for (size_t i = 0U; i < n; i++) {
            switch (proto_rx_feed(&rx, chunk[i])) {
            case PROTO_RX_FRAME:
                on_frame();
                break;
            case PROTO_RX_BAD_CS:
                stats.bad_cs++;
                LOG_W("bad CS (rx %02X calc %02X)", rx.cs_rx, rx.cs_calc);
                break;
            case PROTO_RX_BAD_HEX:
                stats.bad_cs++;
                LOG_W("bad CS digits");
                break;
            case PROTO_RX_OVERFLOW:
                stats.overflow++;
                LOG_W("frame too long");
                break;
            default:
                break;
            }
        }
    }
}

void proto_start(void)
{
    if (!uart_link_init()) {
        LOG_E("USART1 init failed");
        return;
    }
    sup_id = sup_register("proto", 3000U);
    TaskHandle_t h = xTaskCreateStatic(proto_task, "proto", PROTO_STACK_WORDS, NULL, PROTO_PRIORITY,
                                       task_stack, &task_tcb);
    configASSERT(h != NULL);
}

void proto_get_stats(proto_stats_t *out)
{
    *out = stats;
}
