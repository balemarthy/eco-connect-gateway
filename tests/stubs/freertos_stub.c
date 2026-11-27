/*
 * SPDX-License-Identifier: MIT
 *
 * Single-threaded fakes of the FreeRTOS calls used by app/io/ (see FreeRTOS.h here).
 */
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

TickType_t stub_tick;
int        stub_sched_running = 1;
void      *stub_current = (void *)0x1000;
void     (*stub_on_block)(void);
unsigned   stub_block_calls;

/* would block: give the hook one chance, then let the timeout pass */
static int blocked(TickType_t wait)
{
    if (wait == 0U) {
        return 0;
    }
    stub_block_calls++;
    if (stub_on_block != NULL) {
        stub_on_block();
        return 1;                                   /* caller retries once */
    }
    return 0;
}

static void time_out(TickType_t wait)
{
    if (wait != portMAX_DELAY) {
        stub_tick += wait;
    }
}

/* ---------------------------------------------------------------- tasks */

BaseType_t xTaskGetSchedulerState(void)
{
    return stub_sched_running ? taskSCHEDULER_RUNNING : taskSCHEDULER_NOT_STARTED;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return stub_current;
}

TickType_t xTaskGetTickCount(void)
{
    return stub_tick;
}

void vTaskDelay(TickType_t ticks)
{
    stub_tick += ticks;
    if (stub_on_block != NULL) {
        stub_on_block();
    }
}

void vTaskSuspendAll(void)
{
}

BaseType_t xTaskResumeAll(void)
{
    return pdFALSE;
}

/* ---------------------------------------------------------------- queues */

QueueHandle_t xQueueCreateStatic(UBaseType_t len, UBaseType_t item, uint8_t *storage, StaticQueue_t *buf)
{
    if (len == 0U || item == 0U || storage == NULL || buf == NULL) {
        return NULL;
    }
    memset(buf, 0, sizeof *buf);
    buf->storage = storage;
    buf->len = (uint32_t)len;
    buf->item = (uint32_t)item;
    return buf;
}

static BaseType_t q_put(QueueHandle_t q, const void *item)
{
    if (q->count >= q->len) {
        return pdFAIL;
    }
    uint32_t slot = (q->head + q->count) % q->len;
    memcpy(&q->storage[slot * q->item], item, q->item);
    q->count++;
    return pdPASS;
}

static BaseType_t q_get(QueueHandle_t q, void *item)
{
    if (q->count == 0U) {
        return pdFAIL;
    }
    memcpy(item, &q->storage[q->head * q->item], q->item);
    q->head = (q->head + 1U) % q->len;
    q->count--;
    return pdPASS;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait)
{
    if (q_put(q, item) == pdPASS) {
        return pdPASS;
    }
    if (blocked(wait) && q_put(q, item) == pdPASS) {
        return pdPASS;
    }
    time_out(wait);
    return pdFAIL;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait)
{
    if (q_get(q, item) == pdPASS) {
        return pdPASS;
    }
    if (blocked(wait) && q_get(q, item) == pdPASS) {
        return pdPASS;
    }
    time_out(wait);
    return pdFAIL;
}

BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken)
{
    if (woken != NULL) {
        *woken = pdFALSE;
    }
    return q_put(q, item);
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken)
{
    if (woken != NULL) {
        *woken = pdFALSE;
    }
    return q_get(q, item);
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t q)
{
    return (UBaseType_t)q->count;
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    q->head = 0U;
    q->count = 0U;
    return pdPASS;
}

/* ---------------------------------------------------------------- semaphores */

static SemaphoreHandle_t sem_create(StaticSemaphore_t *buf, uint32_t initial, int is_mutex)
{
    if (buf == NULL) {
        return NULL;
    }
    memset(buf, 0, sizeof *buf);
    buf->len = 1U;
    buf->count = initial;
    buf->is_mutex = is_mutex;
    return buf;
}

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *buf)
{
    return sem_create(buf, 0U, 0);
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buf)
{
    return sem_create(buf, 1U, 1);
}

static BaseType_t s_take(SemaphoreHandle_t s)
{
    if (s->count == 0U) {
        return pdFALSE;
    }
    s->count--;
    return pdTRUE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    if (s_take(s) == pdTRUE) {
        return pdTRUE;
    }
    if (blocked(wait) && s_take(s) == pdTRUE) {
        return pdTRUE;
    }
    time_out(wait);
    return pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    if (s->count >= s->len) {
        return pdFALSE;
    }
    s->count++;
    return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken)
{
    if (woken != NULL) {
        *woken = pdFALSE;
    }
    return xSemaphoreGive(s);
}
