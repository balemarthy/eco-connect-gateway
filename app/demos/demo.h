/*
 * Optional demo task, selected at build time (make DEMO=tcp_echo|mqtt|plusio|ota).
 *
 * Exactly one demo is linked; it provides demo_start(). main() calls it after the
 * other *_start() functions and before vTaskStartScheduler(), so the same rules
 * apply: create static objects and the task, no blocking, no HAL delays.
 * DEMO=none (default) compiles none of this.
 */
#ifndef DEMO_H
#define DEMO_H

#include "app_config.h"

void demo_start(void);

#endif /* DEMO_H */
