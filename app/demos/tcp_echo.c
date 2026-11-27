/*
 * TCP echo demo (make DEMO=tcp_echo): RFC 862 style echo server on TCP_ECHO_PORT.
 *
 * netconn API, because lwIP is built with LWIP_NETCONN 1 and LWIP_SOCKET 0
 * (lwipopts.h); the HTTP server uses the same API. One client at a time, which
 * matches the pool sizes (4 TCP PCBs, 2 listeners: HTTP :80 and this one).
 *
 *   accept (1 s timeout, check in) -> recv (1 s timeout, check in) -> write back
 *   close when the peer closes, after TCP_ECHO_RECV_TIMEOUT_MS of silence, after
 *   TCP_ECHO_MAX_BYTES, or when a write does not complete within the send timeout.
 *
 * Test from a PC on the same LAN:  ncat 192.168.1.50 7   (type, see it echoed)
 */
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/api.h"

#define LOG_TAG LOG_MOD_DEMO
#include "log.h"

#include "demo.h"
#include "net_init.h"
#include "supervisor.h"

#define ECHO_POLL_MS     1000U       /* accept / recv timeout: the loop checks in this often */
#define ECHO_SEND_MS     2000        /* a peer that stops reading is dropped after this */

static StaticTask_t task_tcb;
static StackType_t  task_stack[DEMO_TASK_STACK_WORDS];
static sup_id_t     sup_id = SUP_INVALID;

static uint32_t clients;
static uint32_t bytes_echoed;

/* Echoes until the peer closes, goes quiet, sends too much, or stops reading. */
static void serve(struct netconn *c)
{
    uint32_t idle_ms = 0U;
    uint32_t n = 0U;
    const char *why = "peer closed";

    netconn_set_recvtimeout(c, (int)ECHO_POLL_MS);
    netconn_set_sendtimeout(c, ECHO_SEND_MS);
    for (;;) {
        struct netbuf *nb = NULL;
        err_t e = netconn_recv(c, &nb);
        sup_checkin(sup_id);
        if (e == ERR_TIMEOUT) {
            idle_ms += ECHO_POLL_MS;
            if (idle_ms >= TCP_ECHO_RECV_TIMEOUT_MS) {
                why = "idle";
                break;
            }
            continue;
        }
        if (e != ERR_OK) {
            break;                                   /* ERR_CLSD: peer closed; others: reset */
        }
        idle_ms = 0U;
        bool ok = true;
        do {
            void *data;
            u16_t len;
            netbuf_data(nb, &data, &len);
            if (netconn_write(c, data, len, NETCONN_COPY) != ERR_OK) {
                ok = false;
                break;
            }
            n += len;
        } while (netbuf_next(nb) >= 0);
        netbuf_delete(nb);
        if (!ok) {
            why = "write failed";
            break;
        }
        if (n >= TCP_ECHO_MAX_BYTES) {
            why = "byte limit";
            break;
        }
    }
    bytes_echoed += n;
    LOG_I("client done: %lu bytes (%s)", (unsigned long)n, why);
}

static void echo_task(void *arg)
{
    (void)arg;
    while (!net_stack_ready()) {
        sup_checkin(sup_id);
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    struct netconn *listener = netconn_new(NETCONN_TCP);
    if (listener == NULL || netconn_bind(listener, IP_ADDR_ANY, (u16_t)TCP_ECHO_PORT) != ERR_OK ||
        netconn_listen(listener) != ERR_OK) {
        LOG_E("echo: cannot listen on :%u", (unsigned)TCP_ECHO_PORT);
        for (;;) {                                   /* keep checking in: the gateway still works */
            sup_checkin(sup_id);
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }
    netconn_set_recvtimeout(listener, (int)ECHO_POLL_MS);
    LOG_I("echo: listening :%u", (unsigned)TCP_ECHO_PORT);

    for (;;) {
        struct netconn *c = NULL;
        err_t e = netconn_accept(listener, &c);
        sup_checkin(sup_id);
        if (e != ERR_OK) {
            continue;                                /* timeout: nobody connected */
        }
        clients++;
        LOG_I("echo: client %lu", (unsigned long)clients);
        serve(c);
        (void)netconn_close(c);
        (void)netconn_delete(c);
    }
}

void demo_start(void)
{
    /* longest silence: one recv/accept timeout plus one blocked write */
    sup_id = sup_register("echo", 8000U);
    TaskHandle_t h = xTaskCreateStatic(echo_task, "echo", DEMO_TASK_STACK_WORDS, NULL,
                                       tskIDLE_PRIORITY + DEMO_TASK_PRIORITY, task_stack, &task_tcb);
    configASSERT(h != NULL);
}
