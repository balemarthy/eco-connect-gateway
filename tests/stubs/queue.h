/* SPDX-License-Identifier: MIT */
/* Host stand-in for queue.h (see FreeRTOS.h in this folder). */
#ifndef STUB_QUEUE_H
#define STUB_QUEUE_H

#include "FreeRTOS.h"

typedef StaticQueue_t *QueueHandle_t;

QueueHandle_t xQueueCreateStatic(UBaseType_t len, UBaseType_t item, uint8_t *storage, StaticQueue_t *buf);
BaseType_t    xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t    xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
BaseType_t    xQueueSendFromISR(QueueHandle_t q, const void *item, BaseType_t *woken);
BaseType_t    xQueueReceiveFromISR(QueueHandle_t q, void *item, BaseType_t *woken);
UBaseType_t   uxQueueMessagesWaiting(const QueueHandle_t q);
BaseType_t    xQueueReset(QueueHandle_t q);

#endif /* STUB_QUEUE_H */
