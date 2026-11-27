/*
 * Sensor input.
 *
 * A sensor source is a small table of functions. Two sources exist:
 *   sensor_source_sim  - simulated temperature (default, needs no hardware)
 *   sensor_source_i2c  - I2C temperature sensor on I2C1 (PB8/PB9).
 *                        MENTEE TASK: implement it on /I2C1/ of the IO layer (app/io/io.h).
 *                        Until then it returns SENSOR_ERR_NOT_READY.
 * Select with `make SENSOR=i2c` (default `SENSOR=sim`).
 *
 * The sensor task samples on a FreeRTOS software timer and pushes reading_t
 * into the readings queue. Queue full: the oldest reading is dropped (the app
 * always wants the newest) and the drop counter goes up.
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

typedef enum {
    SENSOR_OK = 0,
    SENSOR_ERR_BUS,
    SENSOR_ERR_NOT_READY,
    SENSOR_ERR_RANGE
} sensor_status_t;

typedef struct {
    const char *name;
    sensor_status_t (*init)(void);
    sensor_status_t (*read_centi_c)(int32_t *out);   /* 0.01 degC */
} sensor_source_t;

extern const sensor_source_t sensor_source_sim;
extern const sensor_source_t sensor_source_i2c;

#define SENSOR_INTERVAL_DEFAULT_MS  2000U
#define SENSOR_INTERVAL_MIN_MS      100U
#define SENSOR_INTERVAL_MAX_MS      60000U

/* Creates the readings queue, the sampling timer and the sensor task. */
void sensor_start(void);

QueueHandle_t sensor_readings_queue(void);

bool     sensor_set_interval_ms(uint32_t ms);     /* false if out of range */
uint32_t sensor_get_interval_ms(void);
void     sensor_set_node_id(uint16_t id);
uint16_t sensor_get_node_id(void);
uint32_t sensor_dropped(void);
const char *sensor_source_name(void);

#endif /* SENSOR_H */
