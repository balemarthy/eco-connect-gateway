/*
 * Host tests: Eco-Connect serial protocol (inner packet, outer frame, stream decoder).
 */
#include <stdint.h>

#include "serial_protocol.h"
#include "test_util.h"

/* feed a whole buffer, return the last non-NONE result (or NONE) */
static proto_rx_result_t feed_all(proto_rx_t *rx, const uint8_t *buf, size_t n, int *frames)
{
    proto_rx_result_t last = PROTO_RX_NONE;
    *frames = 0;
    for (size_t i = 0; i < n; i++) {
        proto_rx_result_t r = proto_rx_feed(rx, buf[i]);
        if (r != PROTO_RX_NONE) {
            last = r;
            if (r == PROTO_RX_FRAME) {
                (*frames)++;
            }
        }
    }
    return last;
}

static proto_rx_result_t feed_str(proto_rx_t *rx, const char *s, int *frames)
{
    return feed_all(rx, (const uint8_t *)s, strlen(s), frames);
}

static void checksum_values(void)
{
    const uint8_t a[] = { 0x01, 0x02, 0x03 };
    CHECK_EQ_INT(proto_checksum(a, 3), 0xF9);           /* ~(6) */
    CHECK_EQ_INT(proto_checksum(NULL, 0), 0xFF);
    const uint8_t b[] = { 0xFF, 0x01 };                 /* sum wraps to 0 */
    CHECK_EQ_INT(proto_checksum(b, 2), 0xFF);
}

static void inner_encode_decode(void)
{
    proto_packet_t p, q;
    uint8_t buf[PROTO_INNER_MAX];
    CHECK_EQ_INT(proto_packet_set(&p, PROTO_OP_GET, PROTO_T_STR, "interval"), PROTO_OK);
    size_t n = proto_packet_encode(&p, buf, sizeof buf);
    CHECK_EQ_INT(n, strlen("GET S 08 interval"));
    CHECK(memcmp(buf, "GET S 08 interval", n) == 0);
    CHECK_EQ_INT(proto_packet_decode(buf, n, &q), PROTO_OK);
    CHECK_EQ_INT(q.op, PROTO_OP_GET);
    CHECK_EQ_INT(q.type, 'S');
    CHECK_EQ_INT(q.len, 8);
    CHECK_EQ_STR(q.value, "interval");

    CHECK_EQ_INT(proto_packet_set(&p, PROTO_OP_PING, PROTO_T_NONE, ""), PROTO_OK);
    n = proto_packet_encode(&p, buf, sizeof buf);
    CHECK(memcmp(buf, "PING N 00 ", n) == 0);
    CHECK_EQ_INT(proto_packet_decode(buf, n, &q), PROTO_OK);
    CHECK_EQ_INT(q.op, PROTO_OP_PING);
    CHECK_EQ_INT(q.len, 0);
}

static void inner_errors(void)
{
    proto_packet_t q;
    char longv[PROTO_VALUE_MAX + 2];
    memset(longv, 'x', sizeof longv - 1);
    longv[sizeof longv - 1] = '\0';

    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"FOO S 01 x", 10, &q), PROTO_ERR_OP);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"GET Q 01 x", 10, &q), PROTO_ERR_TYPE);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"GET S 02 x", 10, &q), PROTO_ERR_LEN);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"GET S 00 x", 10, &q), PROTO_ERR_LEN);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"GET S 0G x", 10, &q), PROTO_ERR_FORMAT);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"GET S01 x", 9, &q), PROTO_ERR_FORMAT);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"GET", 3, &q), PROTO_ERR_FORMAT);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"", 0, &q), PROTO_ERR_FORMAT);
    CHECK_EQ_INT(proto_packet_decode((const uint8_t *)"GET S FF x", 10, &q), PROTO_ERR_SPACE);

    proto_packet_t p;
    CHECK_EQ_INT(proto_packet_set(&p, PROTO_OP_SET, PROTO_T_STR, longv), PROTO_ERR_SPACE);
    CHECK_EQ_INT(proto_packet_set(&p, PROTO_OP_COUNT, PROTO_T_STR, "x"), PROTO_ERR_OP);
    CHECK_EQ_INT(proto_packet_set(&p, PROTO_OP_SET, 'x', "x"), PROTO_ERR_TYPE);
}

static void frame_plain(void)
{
    proto_packet_t p;
    uint8_t f[PROTO_FRAME_MAX];
    (void)proto_packet_set(&p, PROTO_OP_GET, PROTO_T_STR, "interval");
    size_t n = proto_build_frame(&p, f, sizeof f);
    /* checksum computed by hand over "GET S 08 interval" */
    const char *inner = "GET S 08 interval";
    uint8_t cs = proto_checksum((const uint8_t *)inner, strlen(inner));
    char expect[64];
    snprintf(expect, sizeof expect, "$%s*%02X\r\n", inner, cs);
    CHECK_EQ_INT(n, strlen(expect));
    CHECK(memcmp(f, expect, n) == 0);
}

static void frame_escaping(void)
{
    /* every special byte inside the value must be escaped and must survive the trip */
    proto_packet_t p, q;
    uint8_t f[PROTO_FRAME_MAX];
    (void)proto_packet_set(&p, PROTO_OP_RSP, PROTO_T_STR, "a$b*c/d\r\ne");
    size_t n = proto_build_frame(&p, f, sizeof f);
    CHECK(n > 0U);
    /* exactly one unescaped '$' (the first byte) */
    int dollars = 0;
    for (size_t i = 0; i < n; i++) {
        if (f[i] == '$' && (i == 0 || f[i - 1] != '/')) {
            dollars++;
        }
    }
    CHECK_EQ_INT(dollars, 1);

    proto_rx_t rx;
    int frames = 0;
    proto_rx_init(&rx);
    CHECK_EQ_INT(feed_all(&rx, f, n, &frames), PROTO_RX_FRAME);
    CHECK_EQ_INT(frames, 1);
    CHECK_EQ_INT(proto_packet_decode(rx.body, rx.len, &q), PROTO_OK);
    CHECK_EQ_STR(q.value, "a$b*c/d\r\ne");
}

static void checksum_symmetry(void)
{
    /* the earlier prototype summed different bytes on TX and RX: every built frame
       must verify, for every opcode and many values */
    proto_rx_t rx;
    uint8_t f[PROTO_FRAME_MAX];
    char v[32];
    int bad = 0;
    for (int op = 0; op < PROTO_OP_COUNT; op++) {
        for (int k = 0; k < 50; k++) {
            proto_packet_t p;
            snprintf(v, sizeof v, "k%d $*/ %d", k, k * 37);
            (void)proto_packet_set(&p, (proto_op_t)op, PROTO_T_STR, v);
            size_t n = proto_build_frame(&p, f, sizeof f);
            int frames = 0;
            proto_rx_init(&rx);
            if (feed_all(&rx, f, n, &frames) != PROTO_RX_FRAME || frames != 1) {
                bad++;
            }
        }
    }
    CHECK_EQ_INT(bad, 0);
}

static void rx_bad_checksum(void)
{
    proto_rx_t rx;
    int frames = 0;
    proto_rx_init(&rx);
    CHECK_EQ_INT(feed_str(&rx, "$GET S 08 interval*00\r\n", &frames), PROTO_RX_BAD_CS);
    CHECK_EQ_INT(frames, 0);
    CHECK_EQ_INT(feed_str(&rx, "$GET S 08 interval*ZZ\r\n", &frames), PROTO_RX_BAD_HEX);
}

static void rx_corrupt_byte(void)
{
    proto_packet_t p;
    uint8_t f[PROTO_FRAME_MAX];
    (void)proto_packet_set(&p, PROTO_OP_SET, PROTO_T_STR, "interval 5000");
    size_t n = proto_build_frame(&p, f, sizeof f);
    f[5] ^= 0x01;                            /* one bit flipped in the body */
    proto_rx_t rx;
    int frames = 0;
    proto_rx_init(&rx);
    CHECK_EQ_INT(feed_all(&rx, f, n, &frames), PROTO_RX_BAD_CS);
}

static void rx_resync_and_noise(void)
{
    proto_packet_t p;
    uint8_t f[PROTO_FRAME_MAX];
    uint8_t stream[3 * PROTO_FRAME_MAX];
    size_t o = 0;

    memcpy(&stream[o], "noise\r\n", 7);
    o += 7;
    memcpy(&stream[o], "$GET S 08 inter", 15);  /* truncated frame, then a new one */
    o += 15;
    (void)proto_packet_set(&p, PROTO_OP_PING, PROTO_T_NONE, "");
    size_t n = proto_build_frame(&p, f, sizeof f);
    memcpy(&stream[o], f, n);
    o += n;
    (void)proto_packet_set(&p, PROTO_OP_GET, PROTO_T_STR, "temp");
    n = proto_build_frame(&p, f, sizeof f);
    memcpy(&stream[o], f, n);
    o += n;

    proto_rx_t rx;
    int frames = 0;
    proto_rx_init(&rx);
    CHECK_EQ_INT(feed_all(&rx, stream, o, &frames), PROTO_RX_FRAME);
    CHECK_EQ_INT(frames, 2);
    proto_packet_t q;
    CHECK_EQ_INT(proto_packet_decode(rx.body, rx.len, &q), PROTO_OK);
    CHECK_EQ_STR(q.value, "temp");
}

static void rx_lowercase_hex_and_no_crlf(void)
{
    const char *inner = "PING N 00 ";
    uint8_t cs = proto_checksum((const uint8_t *)inner, strlen(inner));
    char frame[32];
    snprintf(frame, sizeof frame, "$%s*%02x", inner, cs);   /* lowercase, no CR/LF */
    proto_rx_t rx;
    int frames = 0;
    proto_rx_init(&rx);
    CHECK_EQ_INT(feed_str(&rx, frame, &frames), PROTO_RX_FRAME);
}

static void rx_overflow(void)
{
    proto_rx_t rx;
    int frames = 0;
    proto_rx_result_t last = PROTO_RX_NONE;
    proto_rx_init(&rx);
    (void)proto_rx_feed(&rx, '$');
    for (size_t i = 0; i < PROTO_INNER_MAX + 5U; i++) {
        proto_rx_result_t r = proto_rx_feed(&rx, 'A');
        if (r != PROTO_RX_NONE) {
            last = r;
        }
    }
    CHECK_EQ_INT(last, PROTO_RX_OVERFLOW);
    /* decoder recovers on the next '$' */
    CHECK_EQ_INT(feed_str(&rx, "$PING N 00 *", &frames), PROTO_RX_NONE);
    char cs[3];
    snprintf(cs, sizeof cs, "%02X", proto_checksum((const uint8_t *)"PING N 00 ", 10));
    CHECK_EQ_INT(feed_str(&rx, cs, &frames), PROTO_RX_FRAME);
}

static void frame_small_buffer(void)
{
    proto_packet_t p;
    uint8_t f[10];
    (void)proto_packet_set(&p, PROTO_OP_GET, PROTO_T_STR, "interval");
    CHECK_EQ_INT(proto_build_frame(&p, f, sizeof f), 0);
    uint8_t big[PROTO_FRAME_MAX];
    char v[PROTO_VALUE_MAX + 1];
    memset(v, '$', PROTO_VALUE_MAX);       /* worst case: every value byte escaped */
    v[PROTO_VALUE_MAX] = '\0';
    (void)proto_packet_set(&p, PROTO_OP_PING, PROTO_T_STR, v);
    CHECK(proto_build_frame(&p, big, sizeof big) > 0U);
}

int main(void)
{
    RUN(checksum_values);
    RUN(inner_encode_decode);
    RUN(inner_errors);
    RUN(frame_plain);
    RUN(frame_escaping);
    RUN(checksum_symmetry);
    RUN(rx_bad_checksum);
    RUN(rx_corrupt_byte);
    RUN(rx_resync_and_noise);
    RUN(rx_lowercase_hex_and_no_crlf);
    RUN(rx_overflow);
    RUN(frame_small_buffer);
    TEST_SUMMARY("test_serial_protocol");
}
