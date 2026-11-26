/*
 * Heap-free JSON writer into a caller-provided buffer.
 *
 *     char buf[96];
 *     json_writer_t w;
 *     jw_init(&w, buf, sizeof buf);
 *     jw_object_begin(&w);
 *     jw_kv_int(&w, "node", 1);
 *     jw_kv_fixed2(&w, "value", 2510);          -> 25.10
 *     jw_object_end(&w);
 *     size_t n = jw_finish(&w);                  -> 0 if the buffer was too small
 *
 * Commas are inserted automatically. On overflow every later call is ignored, the
 * buffer always stays NUL-terminated, and jw_finish() returns 0. Pure C, no RTOS.
 */
#ifndef JSON_WRITER_H
#define JSON_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JW_MAX_DEPTH 8U

typedef struct {
    char    *buf;
    size_t   size;
    size_t   len;
    bool     overflow;
    uint8_t  depth;
    uint8_t  need_comma;      /* bit n: depth n already holds a value */
    bool     after_key;       /* a key was written, the value comes next (no comma) */
} json_writer_t;

void   jw_init(json_writer_t *w, char *buf, size_t size);
size_t jw_finish(json_writer_t *w);     /* length without NUL, 0 on overflow or open containers */
bool   jw_ok(const json_writer_t *w);

void jw_object_begin(json_writer_t *w);
void jw_object_end(json_writer_t *w);
void jw_array_begin(json_writer_t *w);
void jw_array_end(json_writer_t *w);
void jw_key(json_writer_t *w, const char *key);

void jw_str(json_writer_t *w, const char *s);
void jw_int(json_writer_t *w, int32_t v);
void jw_uint(json_writer_t *w, uint32_t v);
void jw_fixed2(json_writer_t *w, int32_t centi);     /* 2510 -> 25.10, -5 -> -0.05 */
void jw_bool(json_writer_t *w, bool v);
void jw_null(json_writer_t *w);
/* Insert pre-formatted JSON (e.g. a stored record). Caller guarantees validity. */
void jw_raw(json_writer_t *w, const char *json);

/* key + value shortcuts */
void jw_kv_str(json_writer_t *w, const char *key, const char *s);
void jw_kv_int(json_writer_t *w, const char *key, int32_t v);
void jw_kv_uint(json_writer_t *w, const char *key, uint32_t v);
void jw_kv_fixed2(json_writer_t *w, const char *key, int32_t centi);
void jw_kv_bool(json_writer_t *w, const char *key, bool v);

#endif /* JSON_WRITER_H */
