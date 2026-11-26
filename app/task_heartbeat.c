/*
 * Heartbeat task: toggles LD2 every HEARTBEAT_PERIOD_MS with a fixed period
 * (xTaskDelayUntil, so the period does not drift with the work done in the loop).
 *
 * Every 10 periods it logs "tick n" at DEBUG level. Press B1 to switch the "hb"
 * module between DEBUG and INFO and watch the tick lines start and stop.
 */
#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_HB
#include "log.h"

#include "board.h"
#include "task_heartbeat.h"

#define HB_STACK_WORDS  384U
#define HB_PRIORITY     (tskIDLE_PRIORITY + 1U)
#define HB_LOG_EVERY    10U

static StaticTask_t hb_tcb;
static StackType_t  hb_stack[HB_STACK_WORDS];
static volatile uint32_t hb_count;

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    bool was_pressed = false;

    LOG_I("scheduler started");
    for (;;) {
        board_led_toggle();
        hb_count++;
        if ((hb_count % HB_LOG_EVERY) == 0U) {
            LOG_D("tick %lu", (unsigned long)(hb_count / HB_LOG_EVERY));
        }

        bool pressed = board_button_pressed();
        if (pressed && !was_pressed) {
            log_level_t next = (log_get_level(LOG_MOD_HB) == LOG_LVL_DEBUG) ? LOG_LVL_INFO : LOG_LVL_DEBUG;
            log_set_level(LOG_MOD_HB, next);
            LOG_I("level %s", (next == LOG_LVL_DEBUG) ? "debug" : "info");
        }
        was_pressed = pressed;

        xTaskDelayUntil(&last, pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}

void heartbeat_start(void)
{
    TaskHandle_t h = xTaskCreateStatic(heartbeat_task, "hb", HB_STACK_WORDS, NULL, HB_PRIORITY,
                                       hb_stack, &hb_tcb);
    configASSERT(h != NULL);
}

uint32_t heartbeat_count(void)
{
    return hb_count;
}
