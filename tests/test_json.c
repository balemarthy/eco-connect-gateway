/*
 * Host tests: JSON writer, reading serialiser, flat parser.
 */
#include <stdint.h>

#include "json_parser.h"
#include "json_reading.h"
#include "json_writer.h"
#include "test_util.h"

/* ---------------------------------------------------------------- writer */

static void writer_object(void)
{
    char buf[128];
    json_writer_t w;
    jw_init(&w, buf, sizeof buf);
    jw_object_begin(&w);
    jw_kv_int(&w, "a", -12);
    jw_kv_uint(&w, "b", 4294967295U);
    jw_kv_bool(&w, "c", true);
    jw_key(&w, "d");
    jw_null(&w);
    jw_kv_str(&w, "e", "x");
    jw_object_end(&w);
    CHECK(jw_finish(&w) > 0U);
    CHECK_EQ_STR(buf, "{\"a\":-12,\"b\":4294967295,\"c\":true,\"d\":null,\"e\":\"x\"}");
}

static void writer_nested_and_array(void)
{
    char buf[128];
    json_writer_t w;
    jw_init(&w, buf, sizeof buf);
    jw_object_begin(&w);
    jw_key(&w, "list");
    jw_array_begin(&w);
    jw_int(&w, 1);
    jw_int(&w, 2);
    jw_object_begin(&w);
    jw_kv_int(&w, "x", 3);
    jw_object_end(&w);
    jw_array_end(&w);
    jw_kv_int(&w, "n", 0);
    jw_object_end(&w);
    CHECK_EQ_INT(jw_finish(&w), strlen("{\"list\":[1,2,{\"x\":3}],\"n\":0}"));
    CHECK_EQ_STR(buf, "{\"list\":[1,2,{\"x\":3}],\"n\":0}");
}

static void writer_escape(void)
{
    char buf[64];
    json_writer_t w;
    jw_init(&w, buf, sizeof buf);
    jw_str(&w, "a\"b\\c\n\x01");
    CHECK(jw_finish(&w) > 0U);
    CHECK_EQ_STR(buf, "\"a\\\"b\\\\c\\n\\u0001\"");
}

static void writer_fixed2(void)
{
    char buf[64];
    json_writer_t w;
    jw_init(&w, buf, sizeof buf);
    jw_array_begin(&w);
    jw_fixed2(&w, 2510);
    jw_fixed2(&w, -5);
    jw_fixed2(&w, 0);
    jw_fixed2(&w, -12345);
    jw_fixed2(&w, INT32_MIN);
    jw_array_end(&w);
    CHECK(jw_finish(&w) > 0U);
    CHECK_EQ_STR(buf, "[25.10,-0.05,0.00,-123.45,-21474836.48]");
}

static void writer_overflow(void)
{
    char buf[8];
    json_writer_t w;
    jw_init(&w, buf, sizeof buf);
    jw_object_begin(&w);
    jw_kv_str(&w, "key", "value too long");
    jw_object_end(&w);
    CHECK(!jw_ok(&w));
    CHECK_EQ_INT(jw_finish(&w), 0);
    CHECK_EQ_INT(strlen(buf), 7);               /* filled, still NUL-terminated */
}

static void writer_unbalanced(void)
{
    char buf[32];
    json_writer_t w;
    jw_init(&w, buf, sizeof buf);
    jw_object_begin(&w);
    CHECK_EQ_INT(jw_finish(&w), 0);             /* open object */
    jw_init(&w, buf, sizeof buf);
    jw_object_end(&w);
    CHECK(!jw_ok(&w));                          /* close without open */
}

static void reading_json(void)
{
    char buf[JSON_READING_MAX];
    reading_t r = { .seq = 12, .node = 1, .status = READING_OK, .value_centi = 2510, .timestamp_ms = 34567 };
    size_t n = json_reading_write(buf, sizeof buf, &r);
    CHECK_EQ_STR(buf, "{\"node\":1,\"value\":25.10,\"timestamp\":34567,\"seq\":12}");
    CHECK_EQ_INT(n, strlen(buf));

    reading_t wide = { .seq = UINT32_MAX, .node = UINT16_MAX, .status = READING_SENSOR_ERROR,
                       .value_centi = INT32_MIN, .timestamp_ms = UINT32_MAX };
    n = json_reading_write(buf, sizeof buf, &wide);
    CHECK(n > 0U);                              /* JSON_READING_MAX is big enough */
    CHECK(strstr(buf, "\"status\":\"sensor_error\"") != NULL);
}

/* ---------------------------------------------------------------- parser */

static json_status_t parse(const char *s, json_object_t *o)
{
    return json_parse_object(s, strlen(s), o);
}

static void parser_interval(void)
{
    json_object_t o;
    int32_t v = 0;
    CHECK_EQ_INT(parse("{\"interval_ms\":2000}", &o), JSON_OK);
    CHECK_EQ_INT(o.count, 1);
    CHECK_EQ_INT(json_get_int(&o, "interval_ms", &v), JSON_OK);
    CHECK_EQ_INT(v, 2000);
    CHECK_EQ_INT(json_get_int(&o, "other", &v), JSON_ERR_NOT_FOUND);
}

static void parser_types_and_ws(void)
{
    json_object_t o;
    char s[16];
    bool b = false;
    int32_t v = 0;
    CHECK_EQ_INT(parse(" {\r\n \"cmd\" : \"set\" ,\t\"on\":true, \"n\":-42, \"x\":null, \"f\":1.5e3 } \n", &o), JSON_OK);
    CHECK_EQ_INT(o.count, 5);
    CHECK_EQ_INT(json_get_string(&o, "cmd", s, sizeof s), JSON_OK);
    CHECK_EQ_STR(s, "set");
    CHECK_EQ_INT(json_get_bool(&o, "on", &b), JSON_OK);
    CHECK(b);
    CHECK_EQ_INT(json_get_int(&o, "n", &v), JSON_OK);
    CHECK_EQ_INT(v, -42);
    CHECK_EQ_INT(json_find(&o, "x")->type, JSON_T_NULL);
    CHECK_EQ_INT(json_get_int(&o, "f", &v), JSON_ERR_TYPE);       /* not an integer */
    CHECK_EQ_INT(json_get_int(&o, "cmd", &v), JSON_ERR_TYPE);
    CHECK_EQ_INT(parse("{}", &o), JSON_OK);
    CHECK_EQ_INT(o.count, 0);
}

static void parser_escapes(void)
{
    json_object_t o;
    char s[32];
    CHECK_EQ_INT(parse("{\"k\":\"a\\\"b\\\\c\\n\\u0041\\/\"}", &o), JSON_OK);
    CHECK_EQ_INT(json_get_string(&o, "k", s, sizeof s), JSON_OK);
    CHECK_EQ_STR(s, "a\"b\\c\nA/");
    CHECK_EQ_INT(json_get_string(&o, "k", s, 4), JSON_ERR_RANGE);   /* buffer too small */
}

static void parser_errors(void)
{
    json_object_t o;
    CHECK_EQ_INT(parse("", &o), JSON_ERR_SYNTAX);
    CHECK_EQ_INT(parse("[1]", &o), JSON_ERR_SYNTAX);
    CHECK_EQ_INT(parse("{\"a\" 1}", &o), JSON_ERR_SYNTAX);          /* missing colon */
    CHECK_EQ_INT(parse("{\"a\":1,}", &o), JSON_ERR_SYNTAX);         /* trailing comma */
    CHECK_EQ_INT(parse("{\"a\":1", &o), JSON_ERR_SYNTAX);           /* unterminated object */
    CHECK_EQ_INT(parse("{\"a\":\"x}", &o), JSON_ERR_SYNTAX);        /* unterminated string */
    CHECK_EQ_INT(parse("{\"a\":01}", &o), JSON_ERR_SYNTAX);         /* leading zero */
    CHECK_EQ_INT(parse("{\"a\":1.}", &o), JSON_ERR_SYNTAX);
    CHECK_EQ_INT(parse("{\"a\":-}", &o), JSON_ERR_SYNTAX);
    CHECK_EQ_INT(parse("{\"a\":tru}", &o), JSON_ERR_SYNTAX);
    CHECK_EQ_INT(parse("{\"a\":\"\\q\"}", &o), JSON_ERR_SYNTAX);     /* bad escape */
    CHECK_EQ_INT(parse("{\"a\":\"\\u12g4\"}", &o), JSON_ERR_SYNTAX);
    CHECK_EQ_INT(parse("{\"a\":1} x", &o), JSON_ERR_SYNTAX);        /* trailing garbage */
    CHECK_EQ_INT(parse("{\"a\":{\"b\":1}}", &o), JSON_ERR_NESTED);
    CHECK_EQ_INT(parse("{\"a\":[1]}", &o), JSON_ERR_NESTED);
    CHECK_EQ_INT(parse("{\"a\":1,\"a\":2}", &o), JSON_ERR_DUPLICATE);
    CHECK_EQ_INT(parse("{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9}", &o),
                 JSON_ERR_TOO_MANY);
    CHECK_EQ_INT(json_parse_object(NULL, 0, &o), JSON_ERR_SYNTAX);
}

static void parser_int_range(void)
{
    json_object_t o;
    int32_t v = 0;
    CHECK_EQ_INT(parse("{\"a\":2147483647,\"b\":-2147483648,\"c\":2147483648,\"d\":99999999999999}", &o), JSON_OK);
    CHECK_EQ_INT(json_get_int(&o, "a", &v), JSON_OK);
    CHECK_EQ_INT(v, 2147483647);
    CHECK_EQ_INT(json_get_int(&o, "b", &v), JSON_OK);
    CHECK_EQ_INT(v, INT32_MIN);
    CHECK_EQ_INT(json_get_int(&o, "c", &v), JSON_ERR_RANGE);
    CHECK_EQ_INT(json_get_int(&o, "d", &v), JSON_ERR_RANGE);
}

static void parser_not_nul_terminated(void)
{
    /* the HTTP body is a slice of a larger buffer: parse must stop at len */
    const char text[] = "{\"interval_ms\":500}GARBAGE";
    json_object_t o;
    int32_t v = 0;
    CHECK_EQ_INT(json_parse_object(text, 19, &o), JSON_OK);
    CHECK_EQ_INT(json_get_int(&o, "interval_ms", &v), JSON_OK);
    CHECK_EQ_INT(v, 500);
}

static void round_trip(void)
{
    char buf[JSON_READING_MAX];
    reading_t r = { .seq = 7, .node = 3, .status = READING_OK, .value_centi = -250, .timestamp_ms = 10 };
    size_t n = json_reading_write(buf, sizeof buf, &r);
    json_object_t o;
    int32_t v = 0;
    CHECK_EQ_INT(json_parse_object(buf, n, &o), JSON_OK);
    CHECK_EQ_INT(json_get_int(&o, "seq", &v), JSON_OK);
    CHECK_EQ_INT(v, 7);
    CHECK_EQ_INT(json_get_int(&o, "node", &v), JSON_OK);
    CHECK_EQ_INT(v, 3);
    CHECK_EQ_INT(json_find(&o, "value")->type, JSON_T_NUMBER);
}

int main(void)
{
    RUN(writer_object);
    RUN(writer_nested_and_array);
    RUN(writer_escape);
    RUN(writer_fixed2);
    RUN(writer_overflow);
    RUN(writer_unbalanced);
    RUN(reading_json);
    RUN(parser_interval);
    RUN(parser_types_and_ws);
    RUN(parser_escapes);
    RUN(parser_errors);
    RUN(parser_int_range);
    RUN(parser_not_nul_terminated);
    RUN(round_trip);
    TEST_SUMMARY("test_json");
}
