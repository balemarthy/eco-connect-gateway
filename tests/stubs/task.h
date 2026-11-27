/* SPDX-License-Identifier: MIT */
/* Host stand-in for task.h (see FreeRTOS.h in this folder). */
#ifndef STUB_TASK_H
#define STUB_TASK_H

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

#define taskSCHEDULER_SUSPENDED   ((BaseType_t)0)
#define taskSCHEDULER_NOT_STARTED ((BaseType_t)1)
#define taskSCHEDULER_RUNNING     ((BaseType_t)2)

BaseType_t   xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t   xTaskGetTickCount(void);
void         vTaskDelay(TickType_t ticks);
void         vTaskSuspendAll(void);
BaseType_t   xTaskResumeAll(void);

#endif /* STUB_TASK_H */
