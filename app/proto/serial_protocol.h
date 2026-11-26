/*
 * Eco-Connect serial protocol: an outer frame carrying an inner command packet.
 *
 * Outer frame (link layer, what travels on USART1):
 *
 *     '$'  BODY  '*'  CS_HI CS_LO  [CR] [LF]
 *
 *   BODY   the inner packet, escaped: '$', '*', '/', CR and LF inside it are sent
 *          as '/' followed by the byte itself. After '/' the next byte is always data.
 *   CS     two hex digits: one's complement of the 8-bit sum of the inner packet
 *          bytes (after un-escaping). The sender and the receiver sum the same
 *          bytes, so a frame that was built correctly always verifies.
 *   An unescaped '$' always starts a new frame (resynchronisation after noise).
 *   CR/LF after the checksum are optional; they make the link readable in a terminal.
 *
 * Inner packet (application layer):
 *
 *     OP ' ' TYPE ' ' LL ' ' VALUE
 *
 *   OP     GET | SET | RSP | ERR | PING
 *   TYPE   'N' none, 'I' integer, 'S' string, 'J' JSON
 *   LL     length of VALUE, two hex digits (00..C8)
 *   VALUE  LL bytes
 *
 *   GET:  VALUE = key                         $GET S 08 interval*xx
 *   SET:  VALUE = key ' ' value               $SET S 0D interval 5000*xx
 *   RSP:  VALUE = result (usually JSON)       $RSP J 14 {"interval_ms":5000}*xx
 *   ERR:  VALUE = JSON error                  $ERR J .. {"error":"unknown key"}*xx
 *   PING: VALUE empty, answered by RSP S "pong"
 *
 * Pure C (no RTOS, no HAL): built for the target and for the host tests.
 */
#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_SOF          '$'
#define PROTO_CS_MARK      '*'
#define PROTO_ESC          '/'

#define PROTO_VALUE_MAX    200U
#define PROTO_INNER_MAX    (5U + 1U + 1U + 1U + 2U + 1U + PROTO_VALUE_MAX)   /* "PING N LL value" */
#define PROTO_FRAME_MAX    (1U + 2U * PROTO_INNER_MAX + 1U + 2U + 2U)       /* all bytes escaped */

typedef enum {
    PROTO_OP_GET = 0,
    PROTO_OP_SET,
    PROTO_OP_RSP,
    PROTO_OP_ERR,
    PROTO_OP_PING,
    PROTO_OP_COUNT
} proto_op_t;

typedef enum {
    PROTO_T_NONE = 'N',
    PROTO_T_INT  = 'I',
    PROTO_T_STR  = 'S',
    PROTO_T_JSON = 'J'
} proto_type_t;

typedef struct {
    proto_op_t op;
    char       type;                         /* proto_type_t */
    uint8_t    len;
    char       value[PROTO_VALUE_MAX + 1U];  /* NUL-terminated for convenience */
} proto_packet_t;

typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_FORMAT,       /* inner packet does not follow OP TYPE LL VALUE */
    PROTO_ERR_OP,           /* unknown opcode */
    PROTO_ERR_TYPE,         /* unknown value type */
    PROTO_ERR_LEN,          /* LL does not match the bytes present */
    PROTO_ERR_SPACE         /* output buffer too small / value too long */
} proto_status_t;

/* ---- inner packet ---- */
proto_status_t proto_packet_set(proto_packet_t *p, proto_op_t op, char type, const char *value);
size_t         proto_packet_encode(const proto_packet_t *p, uint8_t *out, size_t size);
proto_status_t proto_packet_decode(const uint8_t *in, size_t len, proto_packet_t *out);

/* ---- outer frame ---- */
uint8_t proto_checksum(const uint8_t *data, size_t len);
size_t  proto_frame_encode(const uint8_t *inner, size_t len, uint8_t *out, size_t size);
/* inner packet -> complete frame in one call; returns frame length or 0 */
size_t  proto_build_frame(const proto_packet_t *p, uint8_t *out, size_t size);

/* ---- streaming frame decoder: feed every received byte ---- */
typedef enum {
    PROTO_RX_NONE = 0,      /* nothing complete yet */
    PROTO_RX_FRAME,         /* body + checksum OK: read rx->body / rx->len */
    PROTO_RX_BAD_CS,        /* checksum mismatch */
    PROTO_RX_OVERFLOW,      /* body longer than PROTO_INNER_MAX */
    PROTO_RX_BAD_HEX        /* checksum digits are not hex */
} proto_rx_result_t;

typedef struct {
    uint8_t state;
    uint8_t cs_hi;
    size_t  len;
    uint8_t body[PROTO_INNER_MAX];
    uint8_t cs_rx;          /* last received checksum (for diagnostics) */
    uint8_t cs_calc;        /* last computed checksum */
} proto_rx_t;

void              proto_rx_init(proto_rx_t *rx);
proto_rx_result_t proto_rx_feed(proto_rx_t *rx, uint8_t byte);

const char *proto_op_name(proto_op_t op);
const char *proto_status_str(proto_status_t st);

#endif /* SERIAL_PROTOCOL_H */
