#ifndef TASK_HEARTBEAT_H
#define TASK_HEARTBEAT_H

#include <stdint.h>

#define HEARTBEAT_PERIOD_MS  500U

/* Creates the heartbeat task (static memory). Call before vTaskStartScheduler(). */
void heartbeat_start(void);
uint32_t heartbeat_count(void);

#endif /* TASK_HEARTBEAT_H */
