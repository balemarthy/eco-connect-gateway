/*
 * Heap-free JSON writer.
 */
#include <string.h>

#include "json_writer.h"

static void put_char(json_writer_t *w, char c)
{
    if (w->overflow) {
        return;
    }
    if (w->len + 1U >= w->size) {          /* keep one byte for the NUL */
        w->overflow = true;
        return;
    }
    w->buf[w->len++] = c;
    w->buf[w->len] = '\0';
}

static void put_str(json_writer_t *w, const char *s)
{
    while (*s != '\0') {
        put_char(w, *s++);
    }
}

/* comma handling: called before every value or key */
static void before_value(json_writer_t *w)
{
    if (w->after_key) {
        w->after_key = false;
        return;
    }
    uint8_t bit = (uint8_t)(1U << w->depth);
    if ((w->need_comma & bit) != 0U) {
        put_char(w, ',');
    }
    w->need_comma |= bit;
}

static void put_uint(json_writer_t *w, uint32_t v)
{
    char tmp[10];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    while (i > 0) {
        put_char(w, tmp[--i]);
    }
}

static void put_escaped(json_writer_t *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    put_char(w, '"');
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  put_str(w, "\\\""); break;
        case '\\': put_str(w, "\\\\"); break;
        case '\n': put_str(w, "\\n");  break;
        case '\r': put_str(w, "\\r");  break;
        case '\t': put_str(w, "\\t");  break;
        default:
            if (c < 0x20U) {
                put_str(w, "\\u00");
                put_char(w, hex[c >> 4]);
                put_char(w, hex[c & 0x0FU]);
            } else {
                put_char(w, (char)c);
            }
            break;
        }
    }
    put_char(w, '"');
}

void jw_init(json_writer_t *w, char *buf, size_t size)
{
    w->buf = buf;
    w->size = size;
    w->len = 0U;
    w->overflow = (buf == NULL || size == 0U);
    w->depth = 0U;
    w->need_comma = 0U;
    w->after_key = false;
    if (!w->overflow) {
        buf[0] = '\0';
    }
}

bool jw_ok(const json_writer_t *w)
{
    return !w->overflow;
}

size_t jw_finish(json_writer_t *w)
{
    if (w->overflow || w->depth != 0U || w->after_key) {
        return 0U;
    }
    return w->len;
}

static void open_container(json_writer_t *w, char c)
{
    before_value(w);
    if (w->depth + 1U >= JW_MAX_DEPTH) {
        w->overflow = true;
        return;
    }
    put_char(w, c);
    w->depth++;
    w->need_comma &= (uint8_t)~(1U << w->depth);
}

static void close_container(json_writer_t *w, char c)
{
    if (w->depth == 0U) {
        w->overflow = true;
        return;
    }
    put_char(w, c);
    w->depth--;
}

void jw_object_begin(json_writer_t *w) { open_container(w, '{'); }
void jw_object_end(json_writer_t *w)   { close_container(w, '}'); }
void jw_array_begin(json_writer_t *w)  { open_container(w, '['); }
void jw_array_end(json_writer_t *w)    { close_container(w, ']'); }

void jw_key(json_writer_t *w, const char *key)
{
    before_value(w);
    put_escaped(w, key);
    put_char(w, ':');
    w->after_key = true;
}

void jw_str(json_writer_t *w, const char *s)
{
    before_value(w);
    put_escaped(w, (s != NULL) ? s : "");
}

void jw_uint(json_writer_t *w, uint32_t v)
{
    before_value(w);
    put_uint(w, v);
}

void jw_int(json_writer_t *w, int32_t v)
{
    before_value(w);
    if (v < 0) {
        put_char(w, '-');
        put_uint(w, (uint32_t)(-(int64_t)v));
    } else {
        put_uint(w, (uint32_t)v);
    }
}

void jw_fixed2(json_writer_t *w, int32_t centi)
{
    before_value(w);
    uint32_t a = (centi < 0) ? (uint32_t)(-(int64_t)centi) : (uint32_t)centi;
    if (centi < 0) {
        put_char(w, '-');
    }
    put_uint(w, a / 100U);
    put_char(w, '.');
    put_char(w, (char)('0' + (a % 100U) / 10U));
    put_char(w, (char)('0' + (a % 10U)));
}

void jw_bool(json_writer_t *w, bool v)
{
    before_value(w);
    put_str(w, v ? "true" : "false");
}

void jw_null(json_writer_t *w)
{
    before_value(w);
    put_str(w, "null");
}

void jw_raw(json_writer_t *w, const char *json)
{
    before_value(w);
    put_str(w, json);
}

void jw_kv_str(json_writer_t *w, const char *key, const char *s)       { jw_key(w, key); jw_str(w, s); }
void jw_kv_int(json_writer_t *w, const char *key, int32_t v)           { jw_key(w, key); jw_int(w, v); }
void jw_kv_uint(json_writer_t *w, const char *key, uint32_t v)         { jw_key(w, key); jw_uint(w, v); }
void jw_kv_fixed2(json_writer_t *w, const char *key, int32_t centi)    { jw_key(w, key); jw_fixed2(w, centi); }
void jw_kv_bool(json_writer_t *w, const char *key, bool v)             { jw_key(w, key); jw_bool(w, v); }
