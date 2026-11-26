/*
 * GET-SET command table. Each entry calls the thread-safe API of the module
 * that owns the value; the registry itself holds no state besides the table.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/init.h"

#define LOG_TAG LOG_MOD_MAIN
#include "log.h"

#include "app_task.h"
#include "commands.h"
#include "json_reading.h"
#include "net_init.h"
#include "registry.h"
#include "sensor.h"
#include "supervisor.h"

static const char *const level_names[] = { "none", "error", "warn", "info", "debug" };

static bool level_from_name(const char *s, log_level_t *out)
{
    for (unsigned i = 0; i < sizeof level_names / sizeof level_names[0]; i++) {
        if (strcmp(s, level_names[i]) == 0) {
            *out = (log_level_t)i;
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------- temp */
static reg_status_t get_temp(json_writer_t *w, void *ctx)
{
    (void)ctx;
    reading_t r;
    if (!app_latest(&r)) {
        return REG_ERR_NO_DATA;
    }
    json_reading_members(w, &r);
    return REG_OK;
}

/* ---------------------------------------------------------------- interval */
static reg_status_t get_interval(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_uint(w, "interval_ms", sensor_get_interval_ms());
    return REG_OK;
}

static reg_status_t set_interval(const char *v, void *ctx)
{
    (void)ctx;
    uint32_t ms;
    if (!reg_parse_u32(v, SENSOR_INTERVAL_MIN_MS, SENSOR_INTERVAL_MAX_MS, &ms)) {
        return REG_ERR_VALUE;
    }
    return sensor_set_interval_ms(ms) ? REG_OK : REG_ERR_VALUE;
}

/* ---------------------------------------------------------------- node */
static reg_status_t get_node(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_uint(w, "node", sensor_get_node_id());
    return REG_OK;
}

static reg_status_t set_node(const char *v, void *ctx)
{
    (void)ctx;
    uint32_t id;
    if (!reg_parse_u32(v, 1U, 65535U, &id)) {
        return REG_ERR_VALUE;
    }
    sensor_set_node_id((uint16_t)id);
    return REG_OK;
}

/* ---------------------------------------------------------------- log */
/* {"log":{"default":"info","hb":"debug"}}: only modules away from the default are
   listed, so the answer stays short enough for one serial packet. */
static reg_status_t get_log(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_key(w, "log");
    jw_object_begin(w);
    jw_kv_str(w, "default", level_names[LOG_DEFAULT_LEVEL]);
    for (unsigned m = 0; m < LOG_MOD_COUNT; m++) {
        log_level_t l = log_get_level((log_module_t)m);
        if (l != LOG_DEFAULT_LEVEL) {
            jw_kv_str(w, log_module_name((log_module_t)m), level_names[l]);
        }
    }
    jw_object_end(w);
    return REG_OK;
}

/* "debug" -> every module; "net debug" -> one module */
static reg_status_t set_log(const char *v, void *ctx)
{
    (void)ctx;
    char mod[12];
    log_level_t lvl;
    const char *sp = strchr(v, ' ');

    if (sp == NULL) {
        if (!level_from_name(v, &lvl)) {
            return REG_ERR_VALUE;
        }
        for (unsigned m = 0; m < LOG_MOD_COUNT; m++) {
            log_set_level((log_module_t)m, lvl);
        }
        return REG_OK;
    }
    size_t n = (size_t)(sp - v);
    if (n == 0U || n >= sizeof mod) {
        return REG_ERR_VALUE;
    }
    memcpy(mod, v, n);
    mod[n] = '\0';
    log_module_t id;
    if (!log_module_from_name(mod, &id) || !level_from_name(sp + 1, &lvl)) {
        return REG_ERR_VALUE;
    }
    log_set_level(id, lvl);
    return REG_OK;
}

/* ---------------------------------------------------------------- status */
static reg_status_t get_status(json_writer_t *w, void *ctx)
{
    (void)ctx;
    char ip[16];
    net_ip_string(ip, sizeof ip);
    jw_kv_uint(w, "uptime_ms", (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    jw_kv_uint(w, "readings", app_readings_count());
    jw_kv_uint(w, "dropped", sensor_dropped());
    jw_kv_uint(w, "log_dropped", log_dropped());
    jw_kv_str(w, "state", gw_state_name(gw_state()));
    jw_kv_str(w, "sensor", sensor_source_name());
    jw_kv_bool(w, "link", net_link_up());
    jw_kv_str(w, "ip", ip);
    jw_kv_uint(w, "heap_free", (uint32_t)xPortGetFreeHeapSize());
    jw_kv_uint(w, "heap_min", (uint32_t)xPortGetMinimumEverFreeHeapSize());
    return REG_OK;
}

/* ---------------------------------------------------------------- version */
static reg_status_t get_version(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_str(w, "fw", FW_VERSION);
    jw_kv_str(w, "freertos", tskKERNEL_VERSION_NUMBER);
    jw_kv_str(w, "lwip", LWIP_VERSION_STRING);
    return REG_OK;
}

static const reg_entry_t entries[] = {
    { "temp",     "latest reading",                 get_temp,     NULL,         NULL },
    { "interval", "sampling period in ms",          get_interval, set_interval, NULL },
    { "node",     "node id in readings",            get_node,     set_node,     NULL },
    { "log",      "log levels: <level> or <module> <level>", get_log, set_log, NULL },
    { "status",   "uptime, counters, link, heap",   get_status,   NULL,         NULL },
    { "version",  "firmware and stack versions",    get_version,  NULL,         NULL },
};

void commands_init(void)
{
    for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; i++) {
        reg_status_t st = registry_register(&entries[i]);
        if (st != REG_OK) {
            LOG_E("registry: %s: %s", entries[i].key, reg_status_str(st));
        }
    }
    LOG_I("registry: %u commands", (unsigned)registry_count());
}
