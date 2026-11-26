/*
 * Eco-Connect GET-SET registry: key -> get/set callbacks.
 *
 * One static table, filled once at start-up (before the scheduler), then only
 * read. The serial protocol (GET/SET packets) and the HTTP server (GET /temp,
 * POST /interval) go through it, so a command behaves the same on every
 * interface. No malloc; results are JSON written into a caller buffer.
 *
 *   registry_get("interval", out)          -> {"interval_ms":2000}
 *   registry_set("interval", "5000", out)  -> {"interval_ms":5000}
 *   registry_get("nope", out)              -> REG_ERR_UNKNOWN, out = {"error":"unknown key","key":"nope"}
 *
 * A getter writes object members (key/value pairs) into an open writer; the
 * registry adds the braces. A setter receives the value as text.
 * Pure C (no RTOS): built for the target and for the host tests.
 */
#ifndef REGISTRY_H
#define REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "json_writer.h"

#define REG_MAX_ENTRIES  16U
#define REG_KEY_MAX      16U          /* longest key, without NUL */

typedef enum {
    REG_OK = 0,
    REG_ERR_UNKNOWN,       /* no such key */
    REG_ERR_VALUE,         /* value rejected by the setter */
    REG_ERR_READONLY,      /* key has no setter */
    REG_ERR_WRITEONLY,     /* key has no getter */
    REG_ERR_FULL,          /* table full */
    REG_ERR_DUPLICATE,     /* key already registered */
    REG_ERR_SPACE,         /* output buffer too small */
    REG_ERR_NO_DATA,       /* nothing to report yet (e.g. no reading) */
    REG_ERR_PARAM
} reg_status_t;

typedef reg_status_t (*reg_get_fn)(json_writer_t *w, void *ctx);
typedef reg_status_t (*reg_set_fn)(const char *value, void *ctx);

typedef struct {
    const char *key;
    const char *help;
    reg_get_fn  get;     /* NULL: write-only */
    reg_set_fn  set;     /* NULL: read-only  */
    void       *ctx;
} reg_entry_t;

/* The entry must stay valid for the life of the program (static const). */
reg_status_t registry_register(const reg_entry_t *e);
void         registry_clear(void);                 /* host tests only */
size_t       registry_count(void);
const reg_entry_t *registry_find(const char *key);

/* Results and errors are complete JSON objects in out. */
reg_status_t registry_get(const char *key, char *out, size_t size);
reg_status_t registry_set(const char *key, const char *value, char *out, size_t size);
/* {"keys":[{"key":"interval","rw":"rw","help":"..."}, ...]}; help is left out when !with_help */
reg_status_t registry_list(char *out, size_t size, bool with_help);
/* {"error":"<reason>","key":"<key>"}; returns length, 0 if it does not fit */
size_t       registry_error_json(reg_status_t st, const char *key, char *out, size_t size);

const char  *reg_status_str(reg_status_t st);

/* helpers for setters */
bool reg_parse_u32(const char *s, uint32_t min, uint32_t max, uint32_t *out);

#endif /* REGISTRY_H */
