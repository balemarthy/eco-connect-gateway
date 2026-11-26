/*
 * Heartbeat task: toggles LD2 every HEARTBEAT_PERIOD_MS with a fixed period
 * (xTaskDelayUntil, so the period does not drift with the work done in the loop).
 */
#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "task_heartbeat.h"
#include "uart_console.h"

#define HB_STACK_WORDS  256U
#define HB_PRIORITY     (tskIDLE_PRIORITY + 1U)

static StaticTask_t hb_tcb;
static StackType_t  hb_stack[HB_STACK_WORDS];
static volatile uint32_t hb_count;

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();

    console_puts("scheduler started\r\n");
    for (;;) {
        board_led_toggle();
        hb_count++;
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
