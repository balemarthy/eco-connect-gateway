/*
 * SPDX-License-Identifier: MIT
 *
 * Circular-buffer RX: the RX interrupt stores each byte in a ring, one task reads it.
 *
 * Single producer (ISR) / single consumer (task), so no lock on the data path: head is
 * written only by the ISR and tail only by the task. Both indices run freely (uint32_t)
 * and are masked with size - 1, which is why the size must be a power of two; the ring
 * is full when head - tail == size, so all size bytes are usable. A binary semaphore,
 * given by the ISR after every stored byte, lets the reader sleep instead of poll.
 *
 * io_cb_read() returns as soon as at least one byte is available (like a terminal read
 * with a minimum of one character), so a frame decoder sees bytes without waiting for a
 * full chunk.
 */
#include "io_mode.h"

bool io_cb_size_ok(uint32_t size, uint32_t reserved)
{
    return size >= IO_CB_MIN_SIZE && size <= reserved && (size & (size - 1U)) == 0U;
}

io_status_t io_cb_init(io_cb_t *cb, uint8_t *storage, uint32_t size)
{
    if (cb == NULL || storage == NULL || !io_cb_size_ok(size, size)) {
        return IO_ERR_PARAM;
    }
    if (!cb->created) {
        cb->sem = xSemaphoreCreateBinaryStatic(&cb->sem_buf);
        if (cb->sem == NULL) {
            return IO_ERR_NO_MEMORY;
        }
        cb->created = true;
    }
    taskENTER_CRITICAL();
    cb->buf = storage;
    cb->size = size;
    cb->head = 0U;
    cb->tail = 0U;
    cb->overruns = 0U;
    taskEXIT_CRITICAL();
    (void)xSemaphoreTake(cb->sem, 0);              /* drop a stale "data arrived" */
    return IO_OK;
}

bool io_cb_put_from_isr(io_cb_t *cb, uint8_t b, BaseType_t *woken)
{
    uint32_t h = cb->head;
    if ((uint32_t)(h - cb->tail) >= cb->size) {
        cb->overruns++;
        return false;
    }
    cb->buf[h & (cb->size - 1U)] = b;
    cb->head = h + 1U;                             /* publish after the byte is stored */
    (void)xSemaphoreGiveFromISR(cb->sem, woken);
    return true;
}

uint32_t io_cb_count(const io_cb_t *cb)
{
    return (uint32_t)(cb->head - cb->tail);
}

size_t io_cb_read(io_cb_t *cb, uint8_t *dst, size_t len, uint32_t timeout_ms, io_status_t *st)
{
    *st = IO_OK;
    if (len == 0U) {
        return 0U;
    }
    const TickType_t start = xTaskGetTickCount();
    const TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        uint32_t t = cb->tail;
        uint32_t avail = (uint32_t)(cb->head - t);
        size_t n = 0U;
        while (n < len && avail > 0U) {
            dst[n++] = cb->buf[t & (cb->size - 1U)];
            t++;
            avail--;
        }
        if (n > 0U) {
            cb->tail = t;                          /* frees the space for the ISR */
            return n;
        }
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= wait) {
            *st = IO_ERR_TIMEOUT;
            return 0U;
        }
        (void)xSemaphoreTake(cb->sem, wait - elapsed);
    }
}

void io_cb_clear(io_cb_t *cb)
{
    taskENTER_CRITICAL();
    cb->tail = cb->head;
    taskEXIT_CRITICAL();
    (void)xSemaphoreTake(cb->sem, 0);
}
