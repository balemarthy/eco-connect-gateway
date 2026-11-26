/*
 * Eco-Connect serial protocol: inner packet codec, outer frame codec, streaming decoder.
 */
#include <string.h>

#include "serial_protocol.h"

static const char *const op_names[PROTO_OP_COUNT] = { "GET", "SET", "RSP", "ERR", "PING" };
static const char hex_digits[] = "0123456789ABCDEF";

enum { RX_IDLE = 0, RX_BODY, RX_ESC, RX_CS_HI, RX_CS_LO };

const char *proto_op_name(proto_op_t op)
{
    return ((unsigned)op < PROTO_OP_COUNT) ? op_names[op] : "?";
}

const char *proto_status_str(proto_status_t st)
{
    switch (st) {
    case PROTO_OK:         return "ok";
    case PROTO_ERR_FORMAT: return "bad format";
    case PROTO_ERR_OP:     return "unknown op";
    case PROTO_ERR_TYPE:   return "unknown type";
    case PROTO_ERR_LEN:    return "bad length";
    case PROTO_ERR_SPACE:  return "too long";
    default:               return "?";
    }
}

static bool type_valid(char t)
{
    return t == PROTO_T_NONE || t == PROTO_T_INT || t == PROTO_T_STR || t == PROTO_T_JSON;
}

static int hex_val(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* ---------------------------------------------------------------- inner packet */

proto_status_t proto_packet_set(proto_packet_t *p, proto_op_t op, char type, const char *value)
{
    size_t n = (value != NULL) ? strlen(value) : 0U;
    if ((unsigned)op >= PROTO_OP_COUNT) {
        return PROTO_ERR_OP;
    }
    if (!type_valid(type)) {
        return PROTO_ERR_TYPE;
    }
    if (n > PROTO_VALUE_MAX) {
        return PROTO_ERR_SPACE;
    }
    p->op = op;
    p->type = type;
    p->len = (uint8_t)n;
    if (n > 0U) {
        memcpy(p->value, value, n);
    }
    p->value[n] = '\0';
    return PROTO_OK;
}

size_t proto_packet_encode(const proto_packet_t *p, uint8_t *out, size_t size)
{
    if ((unsigned)p->op >= PROTO_OP_COUNT || !type_valid(p->type) || p->len > PROTO_VALUE_MAX) {
        return 0U;
    }
    const char *name = op_names[p->op];
    size_t nlen = strlen(name);
    size_t total = nlen + 1U + 1U + 1U + 2U + 1U + p->len;
    if (total > size) {
        return 0U;
    }
    size_t o = 0U;
    memcpy(&out[o], name, nlen);
    o += nlen;
    out[o++] = ' ';
    out[o++] = (uint8_t)p->type;
    out[o++] = ' ';
    out[o++] = (uint8_t)hex_digits[p->len >> 4];
    out[o++] = (uint8_t)hex_digits[p->len & 0x0FU];
    out[o++] = ' ';
    memcpy(&out[o], p->value, p->len);
    o += p->len;
    return o;
}

proto_status_t proto_packet_decode(const uint8_t *in, size_t len, proto_packet_t *out)
{
    size_t i = 0U;

    /* OP: letters up to the first space */
    while (i < len && in[i] != ' ') {
        i++;
    }
    if (i == 0U || i == len) {
        return PROTO_ERR_FORMAT;
    }
    proto_op_t op = PROTO_OP_COUNT;
    for (unsigned k = 0U; k < PROTO_OP_COUNT; k++) {
        if (strlen(op_names[k]) == i && memcmp(in, op_names[k], i) == 0) {
            op = (proto_op_t)k;
            break;
        }
    }
    if (op == PROTO_OP_COUNT) {
        return PROTO_ERR_OP;
    }
    i++;                                         /* space */

    /* TYPE ' ' LL ' ' */
    if (len - i < 5U || in[i + 1U] != ' ' || in[i + 4U] != ' ') {
        return PROTO_ERR_FORMAT;
    }
    char type = (char)in[i];
    if (!type_valid(type)) {
        return PROTO_ERR_TYPE;
    }
    int hi = hex_val(in[i + 2U]);
    int lo = hex_val(in[i + 3U]);
    if (hi < 0 || lo < 0) {
        return PROTO_ERR_FORMAT;
    }
    size_t ll = (size_t)((hi << 4) | lo);
    i += 5U;

    if (ll > PROTO_VALUE_MAX) {
        return PROTO_ERR_SPACE;
    }
    if (len - i != ll) {
        return PROTO_ERR_LEN;
    }
    out->op = op;
    out->type = type;
    out->len = (uint8_t)ll;
    if (ll > 0U) {
        memcpy(out->value, &in[i], ll);
    }
    out->value[ll] = '\0';
    return PROTO_OK;
}

/* ---------------------------------------------------------------- outer frame */

uint8_t proto_checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0U;
    for (size_t i = 0U; i < len; i++) {
        sum = (uint8_t)(sum + data[i]);
    }
    return (uint8_t)~sum;
}

static bool needs_escape(uint8_t c)
{
    return c == (uint8_t)PROTO_SOF || c == (uint8_t)PROTO_CS_MARK || c == (uint8_t)PROTO_ESC ||
           c == '\r' || c == '\n';
}

size_t proto_frame_encode(const uint8_t *inner, size_t len, uint8_t *out, size_t size)
{
    size_t o = 0U;
    uint8_t cs = proto_checksum(inner, len);

    if (size < 1U) {
        return 0U;
    }
    out[o++] = (uint8_t)PROTO_SOF;
    for (size_t i = 0U; i < len; i++) {
        if (needs_escape(inner[i])) {
            if (o + 1U >= size) {
                return 0U;
            }
            out[o++] = (uint8_t)PROTO_ESC;
        }
        if (o >= size) {
            return 0U;
        }
        out[o++] = inner[i];
    }
    if (o + 5U > size) {
        return 0U;
    }
    out[o++] = (uint8_t)PROTO_CS_MARK;
    out[o++] = (uint8_t)hex_digits[cs >> 4];
    out[o++] = (uint8_t)hex_digits[cs & 0x0FU];
    out[o++] = '\r';
    out[o++] = '\n';
    return o;
}

size_t proto_build_frame(const proto_packet_t *p, uint8_t *out, size_t size)
{
    uint8_t inner[PROTO_INNER_MAX];
    size_t n = proto_packet_encode(p, inner, sizeof inner);
    if (n == 0U) {
        return 0U;
    }
    return proto_frame_encode(inner, n, out, size);
}

/* ---------------------------------------------------------------- streaming decoder */

void proto_rx_init(proto_rx_t *rx)
{
    rx->state = RX_IDLE;
    rx->len = 0U;
    rx->cs_hi = 0U;
    rx->cs_rx = 0U;
    rx->cs_calc = 0U;
}

proto_rx_result_t proto_rx_feed(proto_rx_t *rx, uint8_t c)
{
    /* an unescaped '$' restarts the frame in every state */
    if (c == (uint8_t)PROTO_SOF && rx->state != RX_ESC) {
        rx->state = RX_BODY;
        rx->len = 0U;
        return PROTO_RX_NONE;
    }

    switch (rx->state) {
    case RX_IDLE:
        return PROTO_RX_NONE;                    /* noise, CR/LF between frames */

    case RX_BODY:
        if (c == (uint8_t)PROTO_ESC) {
            rx->state = RX_ESC;
            return PROTO_RX_NONE;
        }
        if (c == (uint8_t)PROTO_CS_MARK) {
            rx->state = RX_CS_HI;
            return PROTO_RX_NONE;
        }
        break;                                   /* data byte */

    case RX_ESC:
        rx->state = RX_BODY;
        break;                                   /* escaped data byte */

    case RX_CS_HI: {
        int v = hex_val(c);
        if (v < 0) {
            rx->state = RX_IDLE;
            return PROTO_RX_BAD_HEX;
        }
        rx->cs_hi = (uint8_t)v;
        rx->state = RX_CS_LO;
        return PROTO_RX_NONE;
    }

    case RX_CS_LO: {
        int v = hex_val(c);
        rx->state = RX_IDLE;
        if (v < 0) {
            return PROTO_RX_BAD_HEX;
        }
        rx->cs_rx = (uint8_t)((rx->cs_hi << 4) | (uint8_t)v);
        rx->cs_calc = proto_checksum(rx->body, rx->len);
        return (rx->cs_rx == rx->cs_calc) ? PROTO_RX_FRAME : PROTO_RX_BAD_CS;
    }

    default:
        rx->state = RX_IDLE;
        return PROTO_RX_NONE;
    }

    /* store one body byte */
    if (rx->len >= sizeof rx->body) {
        rx->state = RX_IDLE;
        return PROTO_RX_OVERFLOW;
    }
    rx->body[rx->len++] = c;
    return PROTO_RX_NONE;
}
