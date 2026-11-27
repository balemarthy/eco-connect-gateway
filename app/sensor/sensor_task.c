/*
 * Sensor task: software timer -> task notification -> read -> readings queue.
 *
 * The timer callback runs in the timer service task, so it only sends a
 * notification; the (possibly slow, bus-bound) read happens in our own task.
 */
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"

#define LOG_TAG LOG_MOD_SENSOR
#include "log.h"

#include "io.h"
#include "readings.h"
#include "sensor.h"
#include "supervisor.h"

#define SENSOR_STACK_WORDS  384U
#define SENSOR_PRIORITY     (tskIDLE_PRIORITY + 3U)

#ifndef SENSOR_USE_I2C
#define SENSOR_USE_I2C 0
#endif

/* ---------------------------------------------------------------- simulated source */

static sensor_status_t sim_init(void)
{
    return SENSOR_OK;
}

/* 25.00 degC +/- 2.00 triangle with a period of 64 samples, plus a small
   deterministic ripple, so a plot of the readings is easy to recognise. */
static sensor_status_t sim_read(int32_t *out)
{
    static uint32_t n;
    uint32_t phase = n++ % 64U;
    int32_t tri = (phase < 32U) ? (int32_t)phase : (int32_t)(64U - phase);   /* 0..32..0 */
    int32_t ripple = (int32_t)((n * 7U) % 5U) - 2;                             /* -2..+2 */
    *out = 2300 + (tri * 400) / 32 + ripple;
    return SENSOR_OK;
}

const sensor_source_t sensor_source_sim = { "sim", sim_init, sim_read };

/* ---------------------------------------------------------------- I2C source (mentee task) */

static io_handle_t i2c;                 /* "/I2C1/": 100 kHz, PB8 SCL / PB9 SDA, polled */

static sensor_status_t i2c_sensor_init(void)
{
    i2c = io_open("/I2C1/", 0U);
    if (i2c == NULL) {
        return SENSOR_ERR_BUS;
    }
    /* MENTEE TASK
     * 1. io_ioctl(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(<your sensor>)).
     * 2. Probe the sensor (read its ID register with IO_IOCTL_I2C_WRITE_READ) and
     *    configure it (io_write of register + value).
     * 3. Map io_status_t to sensor_status_t (IO_ERR_IO = NACK or bus error).
     */
    return SENSOR_ERR_NOT_READY;
}

static sensor_status_t i2c_sensor_read(int32_t *out)
{
    /* MENTEE TASK
     * Start a conversion (or read the last one), wait the conversion time, read the
     * raw value with IO_IOCTL_I2C_WRITE_READ (io_i2c_xfer_t), convert to 0.01 degC.
     * Return SENSOR_ERR_RANGE for values the sensor cannot produce.
     */
    (void)out;
    return SENSOR_ERR_NOT_READY;
}

const sensor_source_t sensor_source_i2c = { "i2c", i2c_sensor_init, i2c_sensor_read };

/* ---------------------------------------------------------------- task */

#if SENSOR_USE_I2C
static const sensor_source_t *const src = &sensor_source_i2c;
#else
static const sensor_source_t *const src = &sensor_source_sim;
#endif

static StaticQueue_t q_ctrl;
static uint8_t       q_storage[READINGS_QUEUE_DEPTH * sizeof(reading_t)];
static QueueHandle_t q;

static StaticTimer_t tmr_buf;
static TimerHandle_t tmr;

static StaticTask_t task_tcb;
static StackType_t  task_stack[SENSOR_STACK_WORDS];
static TaskHandle_t task;
static sup_id_t     sup_id = SUP_INVALID;

static volatile uint32_t interval_ms = SENSOR_INTERVAL_DEFAULT_MS;
static volatile uint16_t node_id = 1U;
static volatile uint32_t dropped;

static void sample_timer_cb(TimerHandle_t t)
{
    (void)t;
    xTaskNotifyGive(task);
}

static void push_newest(const reading_t *r)
{
    if (xQueueSend(q, r, 0) != pdPASS) {
        reading_t old;
        (void)xQueueReceive(q, &old, 0);      /* drop the oldest */
        dropped++;
        (void)xQueueSend(q, r, 0);
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    reading_t r = { 0 };
    int32_t last_good = 0;
    sensor_status_t init_st = src->init();

    LOG_I("source %s, interval %lu ms%s", src->name, (unsigned long)interval_ms,
          (init_st == SENSOR_OK) ? "" : " (init failed)");
    (void)xTimerStart(tmr, portMAX_DELAY);

    uint32_t fail_run = 0U;
    for (;;) {
        /* wake at least once a second to check in, even with a 60 s interval */
        uint32_t due = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000U));
        sup_checkin(sup_id);
        if (due == 0U) {
            continue;
        }

        int32_t v = 0;
        sensor_status_t st = src->read_centi_c(&v);
        if (st == SENSOR_OK) {
            last_good = v;
            fail_run = 0U;
        } else {
            LOG_D("read failed (%d)", (int)st);
            /* sensor-pull recovery: re-initialise the source every 5th failure in a row */
            if ((++fail_run % 5U) == 0U && src->init() == SENSOR_OK) {
                LOG_I("source %s re-initialised", src->name);
            }
        }
        r.seq++;
        r.node         = node_id;
        r.status       = (st == SENSOR_OK) ? READING_OK : READING_SENSOR_ERROR;
        r.value_centi  = last_good;
        r.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        push_newest(&r);
    }
}

void sensor_start(void)
{
    q = xQueueCreateStatic(READINGS_QUEUE_DEPTH, sizeof(reading_t), q_storage, &q_ctrl);
    configASSERT(q != NULL);
    vQueueAddToRegistry(q, "readings");

    tmr = xTimerCreateStatic("sample", pdMS_TO_TICKS(interval_ms), pdTRUE, NULL, sample_timer_cb, &tmr_buf);
    configASSERT(tmr != NULL);

    sup_id = sup_register("sensor", 3000U);
    task = xTaskCreateStatic(sensor_task, "sensor", SENSOR_STACK_WORDS, NULL, SENSOR_PRIORITY,
                             task_stack, &task_tcb);
    configASSERT(task != NULL);
}

QueueHandle_t sensor_readings_queue(void)
{
    return q;
}

bool sensor_set_interval_ms(uint32_t ms)
{
    if (ms < SENSOR_INTERVAL_MIN_MS || ms > SENSOR_INTERVAL_MAX_MS) {
        return false;
    }
    interval_ms = ms;
    if (tmr != NULL && xTimerChangePeriod(tmr, pdMS_TO_TICKS(ms), pdMS_TO_TICKS(10U)) != pdPASS) {
        return false;
    }
    LOG_I("interval %lu ms", (unsigned long)ms);
    return true;
}

uint32_t sensor_get_interval_ms(void)
{
    return interval_ms;
}

void sensor_set_node_id(uint16_t id)
{
    node_id = id;
}

uint16_t sensor_get_node_id(void)
{
    return node_id;
}

uint32_t sensor_dropped(void)
{
    return dropped;
}

const char *sensor_source_name(void)
{
    return src->name;
}
