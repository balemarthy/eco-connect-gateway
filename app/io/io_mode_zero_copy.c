/*
 * SPDX-License-Identifier: MIT
 *
 * Zero-copy mode: data goes straight from (and to) the caller's buffer; a write mutex
 * says who may use the peripheral meanwhile.
 *
 *   bus lock (SPI, I2C)   FreeRTOS mutex, so priority inheritance applies. The holder
 *                         keeps it across as many transfers as it needs (the ENC28J60
 *                         reads a whole frame under one lock) and releases it itself.
 *   UART TX               binary semaphore. The task obtains it and calls io_write();
 *                         the driver hands it to the TX interrupt (io_zc_pass_to_isr),
 *                         which gives it back after the last byte. Until then the
 *                         buffer belongs to the driver; IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE
 *                         tells the task when it may reuse it.
 *
 * The holder is recorded, so a write from a task that did not obtain the mutex is
 * refused with IO_ERR_NOT_OWNER. Before the scheduler runs there is only one thread:
 * obtain/release succeed without touching the kernel object.
 */
#include "io_mode.h"

static bool scheduler_running(void)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

io_status_t io_zc_init(io_zc_t *zc, bool isr_release)
{
    if (zc == NULL) {
        return IO_ERR_PARAM;
    }
    if (zc->created) {
        return (zc->isr_release == isr_release) ? IO_OK : IO_ERR_STATE;
    }
    zc->sem = isr_release ? xSemaphoreCreateBinaryStatic(&zc->sem_buf) : xSemaphoreCreateMutexStatic(&zc->sem_buf);
    if (zc->sem == NULL) {
        return IO_ERR_NO_MEMORY;
    }
    if (isr_release) {
        (void)xSemaphoreGive(zc->sem);             /* a binary semaphore starts empty */
    }
    zc->isr_release = isr_release;
    zc->holder = NULL;
    zc->taken = false;
    zc->created = true;
    return IO_OK;
}

bool io_zc_held(const io_zc_t *zc)
{
    return zc->created && zc->taken;
}

bool io_zc_caller_holds(const io_zc_t *zc)
{
    if (!scheduler_running()) {
        return true;
    }
    return zc->created && zc->taken && zc->holder == xTaskGetCurrentTaskHandle();
}

io_status_t io_zc_obtain(io_zc_t *zc, uint32_t timeout_ms)
{
    if (!zc->created) {
        return IO_ERR_STATE;
    }
    if (!scheduler_running()) {
        return IO_OK;
    }
    if (io_zc_caller_holds(zc)) {
        return IO_ERR_STATE;                       /* not recursive: release first */
    }
    if (xSemaphoreTake(zc->sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return IO_ERR_TIMEOUT;
    }
    zc->holder = xTaskGetCurrentTaskHandle();
    zc->taken = true;
    return IO_OK;
}

io_status_t io_zc_release(io_zc_t *zc)
{
    if (!zc->created) {
        return IO_ERR_STATE;
    }
    if (!scheduler_running()) {
        return IO_OK;
    }
    if (!io_zc_caller_holds(zc)) {
        return IO_ERR_NOT_OWNER;
    }
    zc->holder = NULL;
    zc->taken = false;
    (void)xSemaphoreGive(zc->sem);
    return IO_OK;
}

void io_zc_pass_to_isr(io_zc_t *zc)
{
    zc->holder = NULL;                             /* still taken, now owned by the ISR */
}

void io_zc_release_from_isr(io_zc_t *zc, BaseType_t *woken)
{
    if (zc->isr_release && zc->taken && zc->holder == NULL) {
        zc->taken = false;
        (void)xSemaphoreGiveFromISR(zc->sem, woken);
    }
}

io_status_t io_zc_wait_complete(io_zc_t *zc, uint32_t timeout_ms)
{
    io_status_t st = io_zc_obtain(zc, timeout_ms);
    if (st != IO_OK) {
        return st;
    }
    return io_zc_release(zc);
}
