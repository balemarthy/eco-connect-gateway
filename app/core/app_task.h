/*
 * Application task: consumes the readings queue and keeps the latest reading
 * for the network side (HTTP, serial link) to read at any time.
 */
#ifndef APP_TASK_H
#define APP_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "readings.h"

void app_start(void);

/* Copy of the newest reading; false until the first one arrives. */
bool     app_latest(reading_t *out);
uint32_t app_readings_count(void);

/* "25.10", "-3.05": fixed point 0.01 to text, no float formatting. */
void app_format_centi(int32_t centi, char *buf, unsigned size);

#endif /* APP_TASK_H */
