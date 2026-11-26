/*
 * A sensor reading as it travels sensor task -> readings queue -> app task.
 * Fixed size, copied by value into a static FreeRTOS queue (no heap).
 */
#ifndef READINGS_H
#define READINGS_H

#include <stdint.h>

typedef enum {
    READING_OK = 0,
    READING_SENSOR_ERROR        /* the read failed; value is the last good one */
} reading_status_t;

typedef struct {
    uint32_t seq;               /* +1 per sample, wraps at 2^32 */
    uint16_t node;              /* node id of the source (configurable, default 1) */
    uint8_t  status;            /* reading_status_t */
    uint8_t  reserved;
    int32_t  value_centi;       /* temperature in 0.01 degC, e.g. 2510 = 25.10 degC */
    uint32_t timestamp_ms;      /* ms since boot (no RTC yet) */
} reading_t;

#define READINGS_QUEUE_DEPTH  8U

#endif /* READINGS_H */
