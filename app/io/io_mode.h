/*
 * SPDX-License-Identifier: MIT
 *
 * Transfer-mode building blocks shared by the IO drivers. All storage is supplied by
 * the caller (the board table reserves it); every kernel object is created static and
 * only once, later selections reset it.
 *
 *   io_mode_polled.c      deadline wait on a ready flag (busy wait, task or pre-scheduler)
 *   io_mode_circ_buf.c    RX ring filled by an ISR, read by one task with a timeout
 *   io_mode_char_queue.c  FreeRTOS queue of characters, TX (ISR drains) or RX (ISR fills)
 *   io_mode_zero_copy.c   write mutex: task-released (bus lock) or ISR-released (UART TX)
 *
 * The ring and the mode rules are plain C; with the FreeRTOS stubs in tests/stubs/ all
 * four files build and run on the host (tests/test_io.c).
 */
#ifndef IO_MODE_H
#define IO_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "io.h"

typedef enum {
    IO_MODE_POLLED = 0,
    IO_MODE_ZERO_COPY,
    IO_MODE_CHAR_QUEUE,
    IO_MODE_CIRC_BUF,
    IO_MODE_COUNT
} io_mode_t;

const char *io_mode_name(io_mode_t m);

/* ------------------------------------------------------------------ polled */
/* Milliseconds from the platform tick (target: HAL_GetTick, host: test clock). */
uint32_t io_port_ms(void);

typedef bool (*io_ready_fn)(void *ctx);
/* Busy-waits until ready(ctx) is true. IO_OK or IO_ERR_TIMEOUT after timeout_ms. */
io_status_t io_polled_wait(io_ready_fn ready, void *ctx, uint32_t timeout_ms);

/* ------------------------------------------------------------------ circular buffer RX */
#define IO_CB_MIN_SIZE  16U             /* sizes are powers of two from 16 up */

typedef struct {
    uint8_t          *buf;
    uint32_t          size;             /* power of two */
    volatile uint32_t head;             /* free-running, written by the ISR only */
    volatile uint32_t tail;             /* free-running, written by the reading task only */
    volatile uint32_t overruns;         /* bytes dropped because the ring was full */
    StaticSemaphore_t sem_buf;
    SemaphoreHandle_t sem;              /* given by the ISR on every stored byte */
    bool              created;
} io_cb_t;

bool        io_cb_size_ok(uint32_t size, uint32_t reserved);
io_status_t io_cb_init(io_cb_t *cb, uint8_t *storage, uint32_t size);
/* ISR side: false (and overruns + 1) if the ring is full. */
bool        io_cb_put_from_isr(io_cb_t *cb, uint8_t b, BaseType_t *woken);
/* Task side: returns as soon as at least one byte is there (up to len), or 0 after
   timeout_ms with *st = IO_ERR_TIMEOUT. */
size_t      io_cb_read(io_cb_t *cb, uint8_t *dst, size_t len, uint32_t timeout_ms, io_status_t *st);
uint32_t    io_cb_count(const io_cb_t *cb);
void        io_cb_clear(io_cb_t *cb);

/* ------------------------------------------------------------------ character queue */
typedef struct {
    StaticQueue_t q_buf;
    QueueHandle_t q;
    uint32_t      len;
    bool          created;
} io_cq_t;

typedef void (*io_kick_fn)(void *ctx);   /* TX: enable the "transmitter empty" interrupt */

io_status_t io_cq_init(io_cq_t *cq, uint8_t *storage, uint32_t len);
/* Queues len chars; calls kick before it would block and once at the end. Returns the
   number queued (< len: *st = IO_ERR_TIMEOUT). */
size_t      io_cq_write(io_cq_t *cq, const uint8_t *src, size_t len, uint32_t timeout_ms,
                        io_kick_fn kick, void *ctx, io_status_t *st);
/* Waits for all len chars or until timeout_ms; returns the number read. */
size_t      io_cq_read(io_cq_t *cq, uint8_t *dst, size_t len, uint32_t timeout_ms, io_status_t *st);
bool        io_cq_get_from_isr(io_cq_t *cq, uint8_t *b, BaseType_t *woken);
bool        io_cq_put_from_isr(io_cq_t *cq, uint8_t b, BaseType_t *woken);
uint32_t    io_cq_count(const io_cq_t *cq);
io_status_t io_cq_wait_empty(io_cq_t *cq, uint32_t timeout_ms);
void        io_cq_clear(io_cq_t *cq);

/* ------------------------------------------------------------------ zero copy (write mutex) */
typedef struct {
    StaticSemaphore_t     sem_buf;
    SemaphoreHandle_t     sem;
    TaskHandle_t volatile holder;       /* task that obtained it; NULL while an ISR owns it */
    volatile bool         taken;
    bool                  isr_release;  /* true: binary semaphore given back by the TX ISR */
    bool                  created;
} io_zc_t;

/* isr_release false: FreeRTOS mutex (priority inheritance), released by the holder.
   isr_release true: binary semaphore, handed to the TX ISR by io_zc_pass_to_isr(). */
io_status_t io_zc_init(io_zc_t *zc, bool isr_release);
io_status_t io_zc_obtain(io_zc_t *zc, uint32_t timeout_ms);
io_status_t io_zc_release(io_zc_t *zc);
/* The caller's write was handed to an ISR; the ISR ends it with io_zc_release_from_isr(). */
void        io_zc_pass_to_isr(io_zc_t *zc);
void        io_zc_release_from_isr(io_zc_t *zc, BaseType_t *woken);
bool        io_zc_caller_holds(const io_zc_t *zc);
bool        io_zc_held(const io_zc_t *zc);
io_status_t io_zc_wait_complete(io_zc_t *zc, uint32_t timeout_ms);

#endif /* IO_MODE_H */
