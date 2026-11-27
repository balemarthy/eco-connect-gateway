/*
 * Per-module log levels over a log task.
 *
 * Callers format into a line on their own stack and queue it (no blocking, no heap).
 * The log task drains the queue to the console. If the queue is full the line is
 * dropped and counted, so a burst of logs can never stall a real-time task.
 *
 * Usage in a .c file:
 *     #define LOG_TAG LOG_MOD_NET
 *     #include "log.h"
 *     LOG_I("netif up %s", ip);
 *
 * Tasks that log need about 1 KB of stack for the formatter.
 * Do not call from an ISR.
 */
#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LOG_LVL_NONE = 0,
    LOG_LVL_ERROR,
    LOG_LVL_WARN,
    LOG_LVL_INFO,
    LOG_LVL_DEBUG
} log_level_t;

/* The "demo" tag exists only in a demo build (make DEMO=...), so the default build is unchanged. */
#if defined(APP_DEMO) && APP_DEMO
#define LOG_MODULES_DEMO(X) X(LOG_MOD_DEMO, "demo")
#else
#define LOG_MODULES_DEMO(X)
#endif

/* One entry per module; the name is the tag printed in the line. */
#define LOG_MODULES(X) \
    X(LOG_MOD_MAIN, "main") \
    X(LOG_MOD_HB,   "hb")   \
    X(LOG_MOD_SPI,  "spi")  \
    X(LOG_MOD_ENC,  "enc")  \
    X(LOG_MOD_NET,  "net")  \
    X(LOG_MOD_SENSOR, "sensor") \
    X(LOG_MOD_APP,  "app")  \
    X(LOG_MOD_PROTO, "proto") \
    X(LOG_MOD_HTTP, "http") \
    X(LOG_MOD_W25Q, "w25q") \
    X(LOG_MOD_FS,   "fs")   \
    X(LOG_MOD_FSM,  "fsm")  \
    X(LOG_MOD_SUP,  "sup")  \
    LOG_MODULES_DEMO(X)

typedef enum {
#define LOG_ENUM_ENTRY(id, name) id,
    LOG_MODULES(LOG_ENUM_ENTRY)
#undef LOG_ENUM_ENTRY
    LOG_MOD_COUNT
} log_module_t;

#define LOG_DEFAULT_LEVEL  LOG_LVL_INFO
#define LOG_LINE_MAX       120U        /* bytes per line including prefix and CRLF */
#define LOG_QUEUE_DEPTH    16U

/* Creates the queue and the log task. Call once before vTaskStartScheduler().
   Before the scheduler runs, log_write() prints directly (polled). */
void log_init(void);

void log_write(log_module_t mod, log_level_t lvl, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

void        log_set_level(log_module_t mod, log_level_t lvl);
log_level_t log_get_level(log_module_t mod);
const char *log_module_name(log_module_t mod);
bool        log_module_from_name(const char *name, log_module_t *out);
uint32_t    log_dropped(void);

#ifndef LOG_TAG
#define LOG_TAG LOG_MOD_MAIN
#endif

#define LOG_E(...) log_write(LOG_TAG, LOG_LVL_ERROR, __VA_ARGS__)
#define LOG_W(...) log_write(LOG_TAG, LOG_LVL_WARN,  __VA_ARGS__)
#define LOG_I(...) log_write(LOG_TAG, LOG_LVL_INFO,  __VA_ARGS__)
#define LOG_D(...) log_write(LOG_TAG, LOG_LVL_DEBUG, __VA_ARGS__)

#endif /* LOG_H */
