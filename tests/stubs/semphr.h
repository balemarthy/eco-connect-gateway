/* SPDX-License-Identifier: MIT */
/* Host stand-in for semphr.h (see FreeRTOS.h in this folder). */
#ifndef STUB_SEMPHR_H
#define STUB_SEMPHR_H

#include "queue.h"

typedef StaticQueue_t *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *buf);   /* starts empty */
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buf);    /* starts free */
BaseType_t        xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t s);
BaseType_t        xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *woken);

#endif /* STUB_SEMPHR_H */
