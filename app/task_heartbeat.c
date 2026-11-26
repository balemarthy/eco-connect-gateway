/*
 * Heartbeat task: toggles LD2 with a fixed period (xTaskDelayUntil, so the period
 * does not drift with the work done in the loop). 500 ms normally, 100 ms when the
 * gateway state machine is in FAULT.
 *
 * Every 10 periods it logs "tick n" at DEBUG level.
 * B1 short press: switch the "hb" module between DEBUG and INFO.
 * B1 held for 3 s: watchdog test - the task stops checking in with the supervisor,
 * which reports "task hb missed" and lets the IWDG reset the board.
 */
#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_HB
#include "log.h"

#include "board.h"
#include "supervisor.h"
#include "task_heartbeat.h"

#define HB_STACK_WORDS     384U
#define HB_PRIORITY        (tskIDLE_PRIORITY + 1U)
#define HB_LOG_EVERY       10U
#define HB_FAULT_PERIOD_MS 100U
#define HB_STALL_HOLD_MS   3000U

static StaticTask_t hb_tcb;
static StackType_t  hb_stack[HB_STACK_WORDS];
static volatile uint32_t hb_count;
static sup_id_t sup_id = SUP_INVALID;

static void stall_forever(void)
{
    LOG_W("watchdog test: hb stops checking in");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    TickType_t pressed_since = 0U;
    bool was_pressed = false;

    LOG_I("scheduler started");
    for (;;) {
        board_led_toggle();
        hb_count++;
        if ((hb_count % HB_LOG_EVERY) == 0U) {
            LOG_D("tick %lu", (unsigned long)(hb_count / HB_LOG_EVERY));
        }

        TickType_t now = xTaskGetTickCount();
        bool pressed = board_button_pressed();
        if (pressed && !was_pressed) {
            pressed_since = now;
        }
        if (pressed && (now - pressed_since) >= pdMS_TO_TICKS(HB_STALL_HOLD_MS)) {
            stall_forever();
        }
        if (!pressed && was_pressed) {                 /* released: short press */
            log_level_t next = (log_get_level(LOG_MOD_HB) == LOG_LVL_DEBUG) ? LOG_LVL_INFO : LOG_LVL_DEBUG;
            log_set_level(LOG_MOD_HB, next);
            LOG_I("level %s", (next == LOG_LVL_DEBUG) ? "debug" : "info");
        }
        was_pressed = pressed;

        sup_checkin(sup_id);
        uint32_t period = (gw_state() == GW_FAULT) ? HB_FAULT_PERIOD_MS : HEARTBEAT_PERIOD_MS;
        xTaskDelayUntil(&last, pdMS_TO_TICKS(period));
    }
}

void heartbeat_start(void)
{
    sup_id = sup_register("hb", 2000U);
    TaskHandle_t h = xTaskCreateStatic(heartbeat_task, "hb", HB_STACK_WORDS, NULL, HB_PRIORITY,
                                       hb_stack, &hb_tcb);
    configASSERT(h != NULL);
}

uint32_t heartbeat_count(void)
{
    return hb_count;
}
