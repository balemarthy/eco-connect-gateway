/*
 * Heap-free parser for flat JSON objects (config and commands), e.g.
 *     {"interval_ms":2000}
 *     {"cmd":"set","key":"log","value":"debug"}
 *
 * The parser does not copy: each pair points into the input text. Nested objects
 * and arrays are rejected (JSON_ERR_NESTED). Strings are validated, including
 * escapes; json_get_string() unescapes into a caller buffer. Pure C, no RTOS.
 */
#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JSON_MAX_PAIRS 8U

typedef enum {
    JSON_OK = 0,
    JSON_ERR_SYNTAX,
    JSON_ERR_NESTED,       /* object/array value: not supported */
    JSON_ERR_TOO_MANY,     /* more than JSON_MAX_PAIRS members */
    JSON_ERR_NOT_FOUND,
    JSON_ERR_TYPE,         /* member has another type */
    JSON_ERR_RANGE,        /* number does not fit, or string buffer too small */
    JSON_ERR_DUPLICATE     /* same key twice */
} json_status_t;

typedef enum {
    JSON_T_STRING,
    JSON_T_NUMBER,
    JSON_T_BOOL,
    JSON_T_NULL
} json_type_t;

typedef struct {
    const char *key;       /* inside the input, without quotes */
    uint16_t    key_len;
    const char *val;       /* string: without quotes (still escaped); others: raw text */
    uint16_t    val_len;
    json_type_t type;
} json_pair_t;

typedef struct {
    json_pair_t pair[JSON_MAX_PAIRS];
    uint8_t     count;
} json_object_t;

/* Parses exactly one object; only whitespace may follow it. */
json_status_t json_parse_object(const char *text, size_t len, json_object_t *out);

const json_pair_t *json_find(const json_object_t *obj, const char *key);
json_status_t json_get_int(const json_object_t *obj, const char *key, int32_t *out);
json_status_t json_get_bool(const json_object_t *obj, const char *key, bool *out);
json_status_t json_get_string(const json_object_t *obj, const char *key, char *buf, size_t size);

const char *json_status_str(json_status_t st);

#endif /* JSON_PARSER_H */
