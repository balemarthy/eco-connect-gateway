/*
 * Log module: level filter, line formatting, static queue, log task.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "log.h"
#include "supervisor.h"
#include "uart_console.h"

#define LOG_TASK_STACK_WORDS  256U
#define LOG_TASK_PRIORITY     (tskIDLE_PRIORITY + 1U)

typedef struct {
    uint16_t len;
    char     text[LOG_LINE_MAX];
} log_line_t;

static const char *const mod_names[LOG_MOD_COUNT] = {
#define LOG_NAME_ENTRY(id, name) [id] = name,
    LOG_MODULES(LOG_NAME_ENTRY)
#undef LOG_NAME_ENTRY
};

static volatile uint8_t mod_level[LOG_MOD_COUNT];
static volatile uint32_t dropped;

static StaticQueue_t q_ctrl;
static uint8_t       q_storage[LOG_QUEUE_DEPTH * sizeof(log_line_t)];
static QueueHandle_t q;

static StaticTask_t task_tcb;
static StackType_t  task_stack[LOG_TASK_STACK_WORDS];
static sup_id_t     sup_id = SUP_INVALID;

static const char level_char[] = { '-', 'E', 'W', 'I', 'D' };

static void log_task(void *arg)
{
    (void)arg;
    static log_line_t line;    /* only this task touches it */

    for (;;) {
        if (xQueueReceive(q, &line, pdMS_TO_TICKS(1000U)) == pdPASS) {
            console_write(line.text, line.len);
        }
        sup_checkin(sup_id);
    }
}

void log_init(void)
{
    for (unsigned i = 0; i < LOG_MOD_COUNT; i++) {
        mod_level[i] = (uint8_t)LOG_DEFAULT_LEVEL;
    }
    q = xQueueCreateStatic(LOG_QUEUE_DEPTH, sizeof(log_line_t), q_storage, &q_ctrl);
    configASSERT(q != NULL);
    sup_id = sup_register("log", 3000U);
    TaskHandle_t h = xTaskCreateStatic(log_task, "log", LOG_TASK_STACK_WORDS, NULL, LOG_TASK_PRIORITY,
                                       task_stack, &task_tcb);
    configASSERT(h != NULL);
}

void log_write(log_module_t mod, log_level_t lvl, const char *fmt, ...)
{
    if ((unsigned)mod >= LOG_MOD_COUNT || lvl == LOG_LVL_NONE || (uint8_t)lvl > mod_level[mod]) {
        return;
    }

    log_line_t line;
    int n = snprintf(line.text, sizeof line.text, "[%c][%s] ", level_char[lvl], mod_names[mod]);
    if (n < 0) {
        return;
    }
    size_t pos = (size_t)n;

    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(&line.text[pos], sizeof line.text - pos, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    pos += (size_t)n;
    if (pos > sizeof line.text - 3U) {     /* truncated: keep room for CRLF + NUL */
        pos = sizeof line.text - 3U;
    }
    line.text[pos++] = '\r';
    line.text[pos++] = '\n';
    line.text[pos]   = '\0';
    line.len = (uint16_t)pos;

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        console_write(line.text, line.len);
        return;
    }
    if (xQueueSend(q, &line, 0) != pdPASS) {
        dropped++;
    }
}

void log_set_level(log_module_t mod, log_level_t lvl)
{
    if ((unsigned)mod < LOG_MOD_COUNT && lvl <= LOG_LVL_DEBUG) {
        mod_level[mod] = (uint8_t)lvl;
    }
}

log_level_t log_get_level(log_module_t mod)
{
    return ((unsigned)mod < LOG_MOD_COUNT) ? (log_level_t)mod_level[mod] : LOG_LVL_NONE;
}

const char *log_module_name(log_module_t mod)
{
    return ((unsigned)mod < LOG_MOD_COUNT) ? mod_names[mod] : "?";
}

bool log_module_from_name(const char *name, log_module_t *out)
{
    for (unsigned i = 0; i < LOG_MOD_COUNT; i++) {
        if (strcmp(name, mod_names[i]) == 0) {
            *out = (log_module_t)i;
            return true;
        }
    }
    return false;
}

uint32_t log_dropped(void)
{
    return dropped;
}
