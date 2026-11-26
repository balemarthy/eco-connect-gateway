/*
 * Application task.
 *
 * Blocks on the readings queue, keeps the newest reading behind a short
 * critical section (a 20-byte copy) and reports it on the log.
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#define LOG_TAG LOG_MOD_APP
#include "log.h"

#include "app_task.h"
#include "json_reading.h"
#include "sensor.h"
#include "storage_task.h"
#include "supervisor.h"

#define APP_STACK_WORDS  512U
#define APP_PRIORITY     (tskIDLE_PRIORITY + 3U)

static StaticTask_t task_tcb;
static StackType_t  task_stack[APP_STACK_WORDS];
static sup_id_t     sup_id = SUP_INVALID;

static reading_t latest;
static bool      have_latest;
static volatile uint32_t count;

void app_format_centi(int32_t centi, char *buf, unsigned size)
{
    uint32_t a = (centi < 0) ? (uint32_t)(-(int64_t)centi) : (uint32_t)centi;
    (void)snprintf(buf, size, "%s%lu.%02lu", (centi < 0) ? "-" : "",
                   (unsigned long)(a / 100U), (unsigned long)(a % 100U));
}

static void app_task(void *arg)
{
    (void)arg;
    QueueHandle_t q = sensor_readings_queue();
    reading_t r;
    char t[16];
    char json[JSON_READING_MAX];

    for (;;) {
        BaseType_t got = xQueueReceive(q, &r, pdMS_TO_TICKS(1000U));
        sup_checkin(sup_id);
        if (got != pdPASS) {
            continue;
        }
        gw_post((r.status == READING_OK) ? GW_EV_READING_OK : GW_EV_READING_FAIL);
        taskENTER_CRITICAL();
        latest = r;
        have_latest = true;
        taskEXIT_CRITICAL();
        count++;
        if (!storage_submit(&r)) {
            LOG_D("storage queue full");
        }

        app_format_centi(r.value_centi, t, sizeof t);
        if (r.status == READING_OK) {
            LOG_I("seq=%lu node=%u t=%sC", (unsigned long)r.seq, (unsigned)r.node, t);
        } else {
            LOG_W("seq=%lu node=%u sensor error (last t=%sC)", (unsigned long)r.seq, (unsigned)r.node, t);
        }
        /* the JSON form that HTTP and the serial link will send: once at INFO, then DEBUG */
        if (json_reading_write(json, sizeof json, &r) > 0U) {
            log_write(LOG_TAG, (count == 1U) ? LOG_LVL_INFO : LOG_LVL_DEBUG, "json %s", json);
        }
    }
}

void app_start(void)
{
    sup_id = sup_register("app", 3000U);
    TaskHandle_t h = xTaskCreateStatic(app_task, "app", APP_STACK_WORDS, NULL, APP_PRIORITY,
                                       task_stack, &task_tcb);
    configASSERT(h != NULL);
}

bool app_latest(reading_t *out)
{
    bool ok;
    taskENTER_CRITICAL();
    ok = have_latest;
    if (ok) {
        *out = latest;
    }
    taskEXIT_CRITICAL();
    return ok;
}

uint32_t app_readings_count(void)
{
    return count;
}
