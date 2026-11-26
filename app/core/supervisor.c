/*
 * Supervisor task: state machine events, check-in table, IWDG.
 */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "lwip/tcpip.h"

#define LOG_TAG LOG_MOD_SUP
#include "log.h"

#include "gateway_fsm.h"
#include "iwdg.h"
#include "net_init.h"
#include "supervisor.h"

#define SUP_STACK_WORDS   512U
#define SUP_PRIORITY      (tskIDLE_PRIORITY + 1U)
#define SUP_EVENT_DEPTH   16U
#define SUP_TCPIP_WINDOW  3000U

typedef struct {
    const char       *name;
    uint32_t          window_ticks;
    volatile TickType_t last;
} sup_entry_t;

static sup_entry_t entries[SUP_MAX_TASKS];
static volatile uint8_t n_entries;

static StaticTask_t task_tcb;
static StackType_t  task_stack[SUP_STACK_WORDS];
static StaticQueue_t ev_ctrl;
static uint8_t       ev_storage[SUP_EVENT_DEPTH * sizeof(uint8_t)];
static QueueHandle_t ev_q;

static gw_fsm_t fsm;
static volatile gw_state_t cur_state = GW_IDLE;
static sup_id_t tcpip_id = SUP_INVALID;

sup_id_t sup_register(const char *name, uint32_t window_ms)
{
    sup_id_t id = SUP_INVALID;
    taskENTER_CRITICAL();
    if (n_entries < SUP_MAX_TASKS) {
        id = (sup_id_t)n_entries;
        entries[id].name = name;
        entries[id].window_ticks = pdMS_TO_TICKS(window_ms);
        entries[id].last = xTaskGetTickCount();
        n_entries++;
    }
    taskEXIT_CRITICAL();
    return id;
}

void sup_checkin(sup_id_t id)
{
    if (id >= 0 && (uint8_t)id < n_entries) {
        entries[id].last = xTaskGetTickCount();
    }
}

void gw_post(gw_event_t ev)
{
    uint8_t e = (uint8_t)ev;
    if (ev_q != NULL) {
        (void)xQueueSend(ev_q, &e, 0);
    }
}

gw_state_t gw_state(void)
{
    return cur_state;
}

/* runs in the tcpip thread: proves it still processes messages */
static void tcpip_alive(void *arg)
{
    (void)arg;
    sup_checkin(tcpip_id);
}

static void apply(gw_event_t ev)
{
    gw_state_t before = fsm.state;
    if (gw_fsm_apply(&fsm, ev)) {
        cur_state = fsm.state;
        log_write(LOG_MOD_FSM, LOG_LVL_INFO, "%s -> %s (%s)", gw_state_name(before), gw_state_name(fsm.state),
                  gw_event_name(ev));
    }
}

static void supervisor_task(void *arg)
{
    (void)arg;
    TickType_t last_report = 0U;
    bool tripped = false;

#if SUP_USE_IWDG
    iwdg_start(SUP_IWDG_TIMEOUT_MS);
    LOG_I("watchdog %u ms, %u tasks watched", (unsigned)SUP_IWDG_TIMEOUT_MS, (unsigned)n_entries);
#else
    LOG_W("watchdog disabled (WATCHDOG=0)");
#endif

    for (;;) {
        /* 1. state machine events (the wait is also the supervisor period) */
        uint8_t e;
        TickType_t now;
        if (xQueueReceive(ev_q, &e, pdMS_TO_TICKS(SUP_PERIOD_MS)) == pdPASS) {
            do {
                if ((gw_event_t)e == GW_EV_REPORT) {
                    last_report = xTaskGetTickCount();
                }
                apply((gw_event_t)e);
            } while (xQueueReceive(ev_q, &e, 0) == pdPASS);
        }
        now = xTaskGetTickCount();
        if (fsm.reporting && (now - last_report) > pdMS_TO_TICKS(GW_REPORT_IDLE_MS)) {
            apply(GW_EV_REPORT_IDLE);
        }

        /* 2. ask the tcpip thread for a sign of life */
        if (tcpip_id != SUP_INVALID) {
            (void)tcpip_try_callback(tcpip_alive, NULL);
        } else if (net_stack_ready()) {
            tcpip_id = sup_register("tcpip", SUP_TCPIP_WINDOW);
        }

        /* 3. check-ins */
        bool all_ok = true;
        for (uint8_t i = 0U; i < n_entries; i++) {
            if ((now - entries[i].last) > entries[i].window_ticks) {
                all_ok = false;
                if (!tripped) {
                    LOG_E("task %s missed", entries[i].name);
                }
            }
        }
        if (!all_ok && !tripped) {
            tripped = true;
            apply(GW_EV_TASK_STALL);
#if SUP_USE_IWDG
            LOG_E("watchdog will reset the board");
#endif
        }

        /* 4. feed only while everybody is healthy */
#if SUP_USE_IWDG
        if (!tripped) {
            iwdg_feed();
        }
#endif
    }
}

void sup_start(void)
{
    gw_fsm_init(&fsm);
    ev_q = xQueueCreateStatic(SUP_EVENT_DEPTH, sizeof(uint8_t), ev_storage, &ev_ctrl);
    configASSERT(ev_q != NULL);
    TaskHandle_t h = xTaskCreateStatic(supervisor_task, "sup", SUP_STACK_WORDS, NULL, SUP_PRIORITY,
                                       task_stack, &task_tcb);
    configASSERT(h != NULL);
}
