/*
 * SPDX-License-Identifier: MIT
 *
 * Host stand-in for the FreeRTOS headers, for the IO layer host tests (tests/test_io.c).
 * Single-threaded fakes with the kernel's names and types: a test tick, a "current task"
 * the test can switch, queues and semaphores over the caller's static storage. When a call
 * would block, the hook stub_on_block (if set) runs once - standing in for an ISR or
 * another task - and the call is retried; if it still cannot proceed the tick advances by
 * the timeout and the call fails, as it would after the timeout on the target.
 * Only what app/io/ uses is provided. Not the kernel; no claim of equal behaviour beyond
 * what the tests rely on.
 */
#ifndef STUB_FREERTOS_H
#define STUB_FREERTOS_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef long          BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t      TickType_t;

#define pdFALSE        ((BaseType_t)0)
#define pdTRUE         ((BaseType_t)1)
#define pdPASS         pdTRUE
#define pdFAIL         pdFALSE
#define portMAX_DELAY  ((TickType_t)0xFFFFFFFFUL)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))          /* 1 kHz tick, as on the target */

#define configASSERT(x) assert(x)

#define taskENTER_CRITICAL() do { } while (0)
#define taskEXIT_CRITICAL()  do { } while (0)
#define portYIELD_FROM_ISR(x) ((void)(x))

/* one storage type for queues and semaphores */
typedef struct {
    uint8_t *storage;      /* NULL for a semaphore */
    uint32_t len;          /* queue length, or the maximum count */
    uint32_t item;         /* item size; 0 for a semaphore */
    uint32_t head;
    uint32_t count;
    int      is_mutex;
} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;

/* test controls (tests/stubs/freertos_stub.c) */
extern TickType_t stub_tick;
extern int        stub_sched_running;              /* 0: before vTaskStartScheduler */
extern void      *stub_current;                    /* xTaskGetCurrentTaskHandle() */
extern void     (*stub_on_block)(void);
extern unsigned   stub_block_calls;

#endif /* STUB_FREERTOS_H */
