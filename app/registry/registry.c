/*
 * GET-SET registry.
 */
#include <string.h>

#include "registry.h"

static const reg_entry_t *table[REG_MAX_ENTRIES];
static size_t n_entries;

const char *reg_status_str(reg_status_t st)
{
    switch (st) {
    case REG_OK:            return "ok";
    case REG_ERR_UNKNOWN:   return "unknown key";
    case REG_ERR_VALUE:     return "bad value";
    case REG_ERR_READONLY:  return "read-only";
    case REG_ERR_WRITEONLY: return "write-only";
    case REG_ERR_FULL:      return "registry full";
    case REG_ERR_DUPLICATE: return "duplicate key";
    case REG_ERR_SPACE:     return "result too long";
    case REG_ERR_NO_DATA:   return "no data";
    case REG_ERR_PARAM:     return "bad request";
    default:                return "?";
    }
}

reg_status_t registry_register(const reg_entry_t *e)
{
    if (e == NULL || e->key == NULL || e->key[0] == '\0' || strlen(e->key) > REG_KEY_MAX ||
        (e->get == NULL && e->set == NULL)) {
        return REG_ERR_PARAM;
    }
    if (registry_find(e->key) != NULL) {
        return REG_ERR_DUPLICATE;
    }
    if (n_entries >= REG_MAX_ENTRIES) {
        return REG_ERR_FULL;
    }
    table[n_entries++] = e;
    return REG_OK;
}

void registry_clear(void)
{
    n_entries = 0U;
}

size_t registry_count(void)
{
    return n_entries;
}

const reg_entry_t *registry_find(const char *key)
{
    if (key == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < n_entries; i++) {
        if (strcmp(table[i]->key, key) == 0) {
            return table[i];
        }
    }
    return NULL;
}

size_t registry_error_json(reg_status_t st, const char *key, char *out, size_t size)
{
    json_writer_t w;
    jw_init(&w, out, size);
    jw_object_begin(&w);
    jw_kv_str(&w, "error", reg_status_str(st));
    if (key != NULL) {
        jw_kv_str(&w, "key", key);
    }
    jw_object_end(&w);
    return jw_finish(&w);
}

static reg_status_t fail(reg_status_t st, const char *key, char *out, size_t size)
{
    (void)registry_error_json(st, key, out, size);
    return st;
}

static reg_status_t run_get(const reg_entry_t *e, char *out, size_t size)
{
    json_writer_t w;
    jw_init(&w, out, size);
    jw_object_begin(&w);
    reg_status_t st = e->get(&w, e->ctx);
    if (st != REG_OK) {
        return fail(st, e->key, out, size);
    }
    jw_object_end(&w);
    if (jw_finish(&w) == 0U) {
        return fail(REG_ERR_SPACE, e->key, out, size);
    }
    return REG_OK;
}

reg_status_t registry_get(const char *key, char *out, size_t size)
{
    const reg_entry_t *e = registry_find(key);
    if (e == NULL) {
        return fail(REG_ERR_UNKNOWN, key, out, size);
    }
    if (e->get == NULL) {
        return fail(REG_ERR_WRITEONLY, key, out, size);
    }
    return run_get(e, out, size);
}

reg_status_t registry_set(const char *key, const char *value, char *out, size_t size)
{
    const reg_entry_t *e = registry_find(key);
    if (e == NULL) {
        return fail(REG_ERR_UNKNOWN, key, out, size);
    }
    if (e->set == NULL) {
        return fail(REG_ERR_READONLY, key, out, size);
    }
    if (value == NULL) {
        return fail(REG_ERR_VALUE, key, out, size);
    }
    reg_status_t st = e->set(value, e->ctx);
    if (st != REG_OK) {
        return fail(st, key, out, size);
    }
    if (e->get == NULL) {                       /* nothing to read back */
        json_writer_t w;
        jw_init(&w, out, size);
        jw_object_begin(&w);
        jw_kv_bool(&w, "ok", true);
        jw_object_end(&w);
        return (jw_finish(&w) > 0U) ? REG_OK : REG_ERR_SPACE;
    }
    return run_get(e, out, size);               /* answer with the new value */
}

reg_status_t registry_list(char *out, size_t size, bool with_help)
{
    json_writer_t w;
    jw_init(&w, out, size);
    jw_object_begin(&w);
    jw_key(&w, "keys");
    jw_array_begin(&w);
    for (size_t i = 0U; i < n_entries; i++) {
        const reg_entry_t *e = table[i];
        jw_object_begin(&w);
        jw_kv_str(&w, "key", e->key);
        jw_kv_str(&w, "rw", (e->get != NULL && e->set != NULL) ? "rw" : (e->get != NULL) ? "r" : "w");
        if (with_help && e->help != NULL) {
            jw_kv_str(&w, "help", e->help);
        }
        jw_object_end(&w);
    }
    jw_array_end(&w);
    jw_object_end(&w);
    if (jw_finish(&w) == 0U) {
        return fail(REG_ERR_SPACE, NULL, out, size);
    }
    return REG_OK;
}

bool reg_parse_u32(const char *s, uint32_t min, uint32_t max, uint32_t *out)
{
    uint64_t v = 0U;
    if (s == NULL || *s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        v = v * 10U + (uint64_t)(*s - '0');
        if (v > 0xFFFFFFFFULL) {
            return false;
        }
    }
    if (v < min || v > max) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}
