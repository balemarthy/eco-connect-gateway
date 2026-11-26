/*
 * Protocol task.
 *
 * Reads the link byte stream, feeds the frame decoder, decodes the inner
 * packet and answers with one frame per request. Requests handled here:
 *   PING          -> RSP S "pong"
 *   GET temp      -> RSP J {"node":..,"value":..,"timestamp":..,"seq":..}
 *   anything else -> ERR J {"error":"..."}
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_PROTO
#include "log.h"

#include "app_task.h"
#include "json_reading.h"
#include "json_writer.h"
#include "proto_task.h"
#include "serial_protocol.h"
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

static void handle(const proto_packet_t *in, proto_packet_t *out)
{
    switch (in->op) {
    case PROTO_OP_PING:
        (void)proto_packet_set(out, PROTO_OP_RSP, PROTO_T_STR, "pong");
        break;
    case PROTO_OP_GET:
        if (strcmp(in->value, "temp") == 0) {
            reading_t r;
            char json[JSON_READING_MAX];
            if (app_latest(&r) && json_reading_write(json, sizeof json, &r) > 0U) {
                (void)proto_packet_set(out, PROTO_OP_RSP, PROTO_T_JSON, json);
            } else {
                error_packet(out, "no reading yet");
            }
        } else {
            error_packet(out, "unknown key");
        }
        break;
    default:
        error_packet(out, "unsupported op");
        break;
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
    TaskHandle_t h = xTaskCreateStatic(proto_task, "proto", PROTO_STACK_WORDS, NULL, PROTO_PRIORITY,
                                       task_stack, &task_tcb);
    configASSERT(h != NULL);
}

void proto_get_stats(proto_stats_t *out)
{
    *out = stats;
}
