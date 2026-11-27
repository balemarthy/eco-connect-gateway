/*
 * SPDX-License-Identifier: MIT
 *
 * Character-queue mode: a static FreeRTOS queue with one-byte items between a task and
 * an interrupt. TX: io_write() copies into the queue and returns once everything is
 * queued; the "transmitter empty" interrupt takes one character per interrupt. RX: the
 * receive interrupt puts every character in; io_read() waits for the requested count.
 * Simple and safe with several writers, at the cost of one queue operation per byte.
 *
 * The queue is created on the first selection; its length is then fixed (a later
 * selection with another length is IO_ERR_STATE), a later selection only empties it.
 */
#include "io_mode.h"

io_status_t io_cq_init(io_cq_t *cq, uint8_t *storage, uint32_t len)
{
    if (cq == NULL || storage == NULL || len == 0U) {
        return IO_ERR_PARAM;
    }
    if (cq->created) {
        if (len != cq->len) {
            return IO_ERR_STATE;
        }
        (void)xQueueReset(cq->q);
        return IO_OK;
    }
    cq->q = xQueueCreateStatic((UBaseType_t)len, 1U, storage, &cq->q_buf);
    if (cq->q == NULL) {
        return IO_ERR_NO_MEMORY;
    }
    cq->len = len;
    cq->created = true;
    return IO_OK;
}

size_t io_cq_write(io_cq_t *cq, const uint8_t *src, size_t len, uint32_t timeout_ms,
                   io_kick_fn kick, void *ctx, io_status_t *st)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    size_t n = 0U;

    *st = IO_OK;
    while (n < len) {
        if (xQueueSend(cq->q, &src[n], 0) == pdPASS) {
            n++;
            continue;
        }
        /* full: make sure the interrupt is draining before this task sleeps */
        if (kick != NULL) {
            kick(ctx);
        }
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= wait || xQueueSend(cq->q, &src[n], wait - elapsed) != pdPASS) {
            *st = IO_ERR_TIMEOUT;
            break;
        }
        n++;
    }
    if (n > 0U && kick != NULL) {
        kick(ctx);
    }
    return n;
}

size_t io_cq_read(io_cq_t *cq, uint8_t *dst, size_t len, uint32_t timeout_ms, io_status_t *st)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    size_t n = 0U;

    *st = IO_OK;
    while (n < len) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t left = (elapsed >= wait) ? 0U : wait - elapsed;
        if (xQueueReceive(cq->q, &dst[n], left) != pdPASS) {
            *st = IO_ERR_TIMEOUT;
            break;
        }
        n++;
    }
    return n;
}

bool io_cq_get_from_isr(io_cq_t *cq, uint8_t *b, BaseType_t *woken)
{
    return xQueueReceiveFromISR(cq->q, b, woken) == pdPASS;
}

bool io_cq_put_from_isr(io_cq_t *cq, uint8_t b, BaseType_t *woken)
{
    return xQueueSendFromISR(cq->q, &b, woken) == pdPASS;
}

uint32_t io_cq_count(const io_cq_t *cq)
{
    return (uint32_t)uxQueueMessagesWaiting(cq->q);
}

io_status_t io_cq_wait_empty(io_cq_t *cq, uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t wait = pdMS_TO_TICKS(timeout_ms);

    while (uxQueueMessagesWaiting(cq->q) != 0U) {
        if (xTaskGetTickCount() - start >= wait) {
            return IO_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return IO_OK;
}

void io_cq_clear(io_cq_t *cq)
{
    (void)xQueueReset(cq->q);
}
