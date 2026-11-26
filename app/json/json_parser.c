/*
 * Heap-free flat JSON object parser.
 */
#include <string.h>

#include "json_parser.h"

typedef struct {
    const char *p;
    const char *end;
} cursor_t;

static void skip_ws(cursor_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) {
        c->p++;
    }
}

static bool is_hex(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static bool is_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

/* c->p at the opening quote. On success s and n span the content, c->p is after the closing quote. */
static json_status_t scan_string(cursor_t *c, const char **s, uint16_t *n)
{
    c->p++;                                     /* opening quote */
    const char *start = c->p;
    while (c->p < c->end) {
        char ch = *c->p;
        if (ch == '"') {
            size_t len = (size_t)(c->p - start);
            if (len > 0xFFFFU) {
                return JSON_ERR_RANGE;
            }
            *s = start;
            *n = (uint16_t)len;
            c->p++;
            return JSON_OK;
        }
        if ((unsigned char)ch < 0x20U) {
            return JSON_ERR_SYNTAX;             /* raw control character */
        }
        if (ch == '\\') {
            c->p++;
            if (c->p >= c->end) {
                return JSON_ERR_SYNTAX;
            }
            switch (*c->p) {
            case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
                break;
            case 'u':
                if (c->end - c->p < 5 || !is_hex(c->p[1]) || !is_hex(c->p[2]) ||
                    !is_hex(c->p[3]) || !is_hex(c->p[4])) {
                    return JSON_ERR_SYNTAX;
                }
                c->p += 4;
                break;
            default:
                return JSON_ERR_SYNTAX;
            }
        }
        c->p++;
    }
    return JSON_ERR_SYNTAX;                     /* unterminated */
}

/* -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)? */
static json_status_t scan_number(cursor_t *c)
{
    const char *p = c->p;
    if (p < c->end && *p == '-') {
        p++;
    }
    if (p >= c->end || !is_digit(*p)) {
        return JSON_ERR_SYNTAX;
    }
    if (*p == '0') {
        p++;
    } else {
        while (p < c->end && is_digit(*p)) {
            p++;
        }
    }
    if (p < c->end && *p == '.') {
        p++;
        if (p >= c->end || !is_digit(*p)) {
            return JSON_ERR_SYNTAX;
        }
        while (p < c->end && is_digit(*p)) {
            p++;
        }
    }
    if (p < c->end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < c->end && (*p == '+' || *p == '-')) {
            p++;
        }
        if (p >= c->end || !is_digit(*p)) {
            return JSON_ERR_SYNTAX;
        }
        while (p < c->end && is_digit(*p)) {
            p++;
        }
    }
    c->p = p;
    return JSON_OK;
}

static bool scan_literal(cursor_t *c, const char *lit)
{
    size_t n = strlen(lit);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, lit, n) != 0) {
        return false;
    }
    c->p += n;
    return true;
}

static bool key_equals(const json_pair_t *pr, const char *key, size_t klen)
{
    return pr->key_len == klen && memcmp(pr->key, key, klen) == 0;
}

json_status_t json_parse_object(const char *text, size_t len, json_object_t *out)
{
    cursor_t c = { text, text + len };
    out->count = 0U;

    if (text == NULL) {
        return JSON_ERR_SYNTAX;
    }
    skip_ws(&c);
    if (c.p >= c.end || *c.p != '{') {
        return JSON_ERR_SYNTAX;
    }
    c.p++;
    skip_ws(&c);
    if (c.p < c.end && *c.p == '}') {
        c.p++;
        goto tail;
    }

    for (;;) {
        json_pair_t pr;

        skip_ws(&c);
        if (c.p >= c.end || *c.p != '"') {
            return JSON_ERR_SYNTAX;
        }
        json_status_t st = scan_string(&c, &pr.key, &pr.key_len);
        if (st != JSON_OK) {
            return st;
        }
        skip_ws(&c);
        if (c.p >= c.end || *c.p != ':') {
            return JSON_ERR_SYNTAX;
        }
        c.p++;
        skip_ws(&c);
        if (c.p >= c.end) {
            return JSON_ERR_SYNTAX;
        }

        const char *vstart = c.p;
        switch (*c.p) {
        case '"':
            pr.type = JSON_T_STRING;
            st = scan_string(&c, &pr.val, &pr.val_len);
            if (st != JSON_OK) {
                return st;
            }
            break;
        case '{':
        case '[':
            return JSON_ERR_NESTED;
        case 't':
        case 'f':
            pr.type = JSON_T_BOOL;
            if (!scan_literal(&c, "true") && !scan_literal(&c, "false")) {
                return JSON_ERR_SYNTAX;
            }
            break;
        case 'n':
            pr.type = JSON_T_NULL;
            if (!scan_literal(&c, "null")) {
                return JSON_ERR_SYNTAX;
            }
            break;
        default:
            pr.type = JSON_T_NUMBER;
            st = scan_number(&c);
            if (st != JSON_OK) {
                return st;
            }
            break;
        }
        if (pr.type != JSON_T_STRING) {
            pr.val = vstart;
            pr.val_len = (uint16_t)(c.p - vstart);
        }

        for (uint8_t i = 0U; i < out->count; i++) {
            if (key_equals(&out->pair[i], pr.key, pr.key_len)) {
                return JSON_ERR_DUPLICATE;
            }
        }
        if (out->count >= JSON_MAX_PAIRS) {
            return JSON_ERR_TOO_MANY;
        }
        out->pair[out->count++] = pr;

        skip_ws(&c);
        if (c.p >= c.end) {
            return JSON_ERR_SYNTAX;
        }
        if (*c.p == ',') {
            c.p++;
            continue;
        }
        if (*c.p == '}') {
            c.p++;
            break;
        }
        return JSON_ERR_SYNTAX;
    }

tail:
    skip_ws(&c);
    /* allow a trailing NUL from fixed buffers */
    while (c.p < c.end && *c.p == '\0') {
        c.p++;
    }
    return (c.p == c.end) ? JSON_OK : JSON_ERR_SYNTAX;
}

const json_pair_t *json_find(const json_object_t *obj, const char *key)
{
    size_t klen = strlen(key);
    for (uint8_t i = 0U; i < obj->count; i++) {
        if (key_equals(&obj->pair[i], key, klen)) {
            return &obj->pair[i];
        }
    }
    return NULL;
}

json_status_t json_get_int(const json_object_t *obj, const char *key, int32_t *out)
{
    const json_pair_t *pr = json_find(obj, key);
    if (pr == NULL) {
        return JSON_ERR_NOT_FOUND;
    }
    if (pr->type != JSON_T_NUMBER) {
        return JSON_ERR_TYPE;
    }
    const char *p = pr->val;
    const char *end = pr->val + pr->val_len;
    bool neg = false;
    if (p < end && *p == '-') {
        neg = true;
        p++;
    }
    int64_t v = 0;
    for (; p < end; p++) {
        if (!is_digit(*p)) {
            return JSON_ERR_TYPE;               /* fraction or exponent: not an integer */
        }
        v = v * 10 + (*p - '0');
        if (v > 2147483648LL) {
            return JSON_ERR_RANGE;
        }
    }
    if (neg) {
        v = -v;
    }
    if (v > INT32_MAX || v < INT32_MIN) {
        return JSON_ERR_RANGE;
    }
    *out = (int32_t)v;
    return JSON_OK;
}

json_status_t json_get_bool(const json_object_t *obj, const char *key, bool *out)
{
    const json_pair_t *pr = json_find(obj, key);
    if (pr == NULL) {
        return JSON_ERR_NOT_FOUND;
    }
    if (pr->type != JSON_T_BOOL) {
        return JSON_ERR_TYPE;
    }
    *out = (pr->val[0] == 't');
    return JSON_OK;
}

static unsigned hex_val(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (unsigned)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (unsigned)(ch - 'a' + 10);
    }
    return (unsigned)(ch - 'A' + 10);
}

json_status_t json_get_string(const json_object_t *obj, const char *key, char *buf, size_t size)
{
    const json_pair_t *pr = json_find(obj, key);
    if (pr == NULL) {
        return JSON_ERR_NOT_FOUND;
    }
    if (pr->type != JSON_T_STRING) {
        return JSON_ERR_TYPE;
    }
    size_t o = 0U;
    for (uint16_t i = 0U; i < pr->val_len; i++) {
        char ch = pr->val[i];
        if (ch == '\\') {
            char e = pr->val[++i];
            switch (e) {
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'u': {
                unsigned cp = (hex_val(pr->val[i + 1U]) << 12) | (hex_val(pr->val[i + 2U]) << 8) |
                              (hex_val(pr->val[i + 3U]) << 4) | hex_val(pr->val[i + 4U]);
                i = (uint16_t)(i + 4U);
                ch = (cp < 0x80U) ? (char)cp : '?';   /* ASCII only; others become '?' */
                break;
            }
            default:   ch = e; break;          /* " \ / */
            }
        }
        if (o + 1U >= size) {
            return JSON_ERR_RANGE;
        }
        buf[o++] = ch;
    }
    if (size == 0U) {
        return JSON_ERR_RANGE;
    }
    buf[o] = '\0';
    return JSON_OK;
}

const char *json_status_str(json_status_t st)
{
    switch (st) {
    case JSON_OK:            return "ok";
    case JSON_ERR_SYNTAX:    return "syntax";
    case JSON_ERR_NESTED:    return "nested";
    case JSON_ERR_TOO_MANY:  return "too many members";
    case JSON_ERR_NOT_FOUND: return "missing";
    case JSON_ERR_TYPE:      return "wrong type";
    case JSON_ERR_RANGE:     return "out of range";
    case JSON_ERR_DUPLICATE: return "duplicate key";
    default:                 return "?";
    }
}
