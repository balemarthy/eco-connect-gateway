/*
 * Host tests: GET-SET registry.
 */
#include <stdint.h>

#include "registry.h"
#include "test_util.h"

static uint32_t interval = 2000;
static int set_calls;

static reg_status_t get_interval(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_uint(w, "interval_ms", interval);
    return REG_OK;
}

static reg_status_t set_interval(const char *v, void *ctx)
{
    (void)ctx;
    uint32_t ms;
    set_calls++;
    if (!reg_parse_u32(v, 100, 60000, &ms)) {
        return REG_ERR_VALUE;
    }
    interval = ms;
    return REG_OK;
}

static reg_status_t get_version(json_writer_t *w, void *ctx)
{
    jw_kv_str(w, "version", (const char *)ctx);
    return REG_OK;
}

static int resets;
static reg_status_t set_reset(const char *v, void *ctx)
{
    (void)v;
    (void)ctx;
    resets++;
    return REG_OK;
}

static reg_status_t get_big(json_writer_t *w, void *ctx)
{
    (void)ctx;
    jw_kv_str(w, "text", "0123456789012345678901234567890123456789");
    return REG_OK;
}

static reg_status_t get_failing(json_writer_t *w, void *ctx)
{
    (void)w;
    (void)ctx;
    return REG_ERR_VALUE;
}

static const reg_entry_t e_interval = { "interval", "sampling period, ms", get_interval, set_interval, NULL };
static const reg_entry_t e_version  = { "version", "firmware version", get_version, NULL, (void *)"1.2.3" };
static const reg_entry_t e_reset    = { "reset", NULL, NULL, set_reset, NULL };
static const reg_entry_t e_big      = { "big", NULL, get_big, NULL, NULL };
static const reg_entry_t e_failing  = { "failing", NULL, get_failing, NULL, NULL };

static void setup(void)
{
    registry_clear();
    interval = 2000;
    set_calls = 0;
    resets = 0;
    CHECK_EQ_INT(registry_register(&e_interval), REG_OK);
    CHECK_EQ_INT(registry_register(&e_version), REG_OK);
    CHECK_EQ_INT(registry_register(&e_reset), REG_OK);
    CHECK_EQ_INT(registry_register(&e_big), REG_OK);
    CHECK_EQ_INT(registry_register(&e_failing), REG_OK);
}

static void get_set_roundtrip(void)
{
    char out[128];
    setup();
    CHECK_EQ_INT(registry_get("interval", out, sizeof out), REG_OK);
    CHECK_EQ_STR(out, "{\"interval_ms\":2000}");
    CHECK_EQ_INT(registry_set("interval", "5000", out, sizeof out), REG_OK);
    CHECK_EQ_STR(out, "{\"interval_ms\":5000}");
    CHECK_EQ_INT(interval, 5000);
    CHECK_EQ_INT(registry_get("version", out, sizeof out), REG_OK);
    CHECK_EQ_STR(out, "{\"version\":\"1.2.3\"}");
}

static void errors(void)
{
    char out[128];
    setup();
    CHECK_EQ_INT(registry_get("nope", out, sizeof out), REG_ERR_UNKNOWN);
    CHECK_EQ_STR(out, "{\"error\":\"unknown key\",\"key\":\"nope\"}");
    CHECK_EQ_INT(registry_set("version", "2", out, sizeof out), REG_ERR_READONLY);
    CHECK_EQ_STR(out, "{\"error\":\"read-only\",\"key\":\"version\"}");
    CHECK_EQ_INT(registry_get("reset", out, sizeof out), REG_ERR_WRITEONLY);
    CHECK_EQ_INT(registry_set("interval", "50", out, sizeof out), REG_ERR_VALUE);
    CHECK_EQ_STR(out, "{\"error\":\"bad value\",\"key\":\"interval\"}");
    CHECK_EQ_INT(interval, 2000);                       /* unchanged */
    CHECK_EQ_INT(registry_set("interval", "12x", out, sizeof out), REG_ERR_VALUE);
    CHECK_EQ_INT(registry_set("interval", NULL, out, sizeof out), REG_ERR_VALUE);
    CHECK_EQ_INT(registry_get("failing", out, sizeof out), REG_ERR_VALUE);
    CHECK_EQ_STR(out, "{\"error\":\"bad value\",\"key\":\"failing\"}");
    CHECK_EQ_INT(registry_get(NULL, out, sizeof out), REG_ERR_UNKNOWN);
}

static void write_only_set(void)
{
    char out[64];
    setup();
    CHECK_EQ_INT(registry_set("reset", "1", out, sizeof out), REG_OK);
    CHECK_EQ_STR(out, "{\"ok\":true}");
    CHECK_EQ_INT(resets, 1);
}

static void small_buffer(void)
{
    char out[24];
    setup();
    CHECK_EQ_INT(registry_get("big", out, sizeof out), REG_ERR_SPACE);
    CHECK_EQ_STR(out, "{\"error\":\"result too lo");   /* error also truncated, but terminated */
    char tiny[4];
    CHECK_EQ_INT(registry_get("interval", tiny, sizeof tiny), REG_ERR_SPACE);
    CHECK_EQ_INT(strlen(tiny) < sizeof tiny, 1);
}

static void table_rules(void)
{
    setup();
    CHECK_EQ_INT(registry_register(&e_interval), REG_ERR_DUPLICATE);
    static const reg_entry_t bad_key = { "", NULL, get_interval, NULL, NULL };
    static const reg_entry_t no_fn = { "x", NULL, NULL, NULL, NULL };
    static const reg_entry_t long_key = { "a_key_that_is_too_long", NULL, get_interval, NULL, NULL };
    CHECK_EQ_INT(registry_register(&bad_key), REG_ERR_PARAM);
    CHECK_EQ_INT(registry_register(&no_fn), REG_ERR_PARAM);
    CHECK_EQ_INT(registry_register(&long_key), REG_ERR_PARAM);
    CHECK_EQ_INT(registry_register(NULL), REG_ERR_PARAM);

    /* fill the table */
    static char names[REG_MAX_ENTRIES][8];
    static reg_entry_t fill[REG_MAX_ENTRIES];
    size_t before = registry_count();
    reg_status_t last = REG_OK;
    for (size_t i = 0; i < REG_MAX_ENTRIES; i++) {
        snprintf(names[i], sizeof names[i], "k%u", (unsigned)i);
        fill[i].key = names[i];
        fill[i].get = get_interval;
        last = registry_register(&fill[i]);
        if (last != REG_OK) {
            break;
        }
    }
    CHECK_EQ_INT(last, REG_ERR_FULL);
    CHECK_EQ_INT(registry_count(), REG_MAX_ENTRIES);
    CHECK(before < REG_MAX_ENTRIES);
}

static void list_keys(void)
{
    char out[256];
    registry_clear();
    (void)registry_register(&e_interval);
    (void)registry_register(&e_version);
    (void)registry_register(&e_reset);
    CHECK_EQ_INT(registry_list(out, sizeof out, true), REG_OK);
    CHECK_EQ_STR(out, "{\"keys\":[{\"key\":\"interval\",\"rw\":\"rw\",\"help\":\"sampling period, ms\"},"
                      "{\"key\":\"version\",\"rw\":\"r\",\"help\":\"firmware version\"},"
                      "{\"key\":\"reset\",\"rw\":\"w\"}]}");
    CHECK_EQ_INT(registry_list(out, sizeof out, false), REG_OK);
    CHECK_EQ_STR(out, "{\"keys\":[{\"key\":\"interval\",\"rw\":\"rw\"},{\"key\":\"version\",\"rw\":\"r\"},"
                      "{\"key\":\"reset\",\"rw\":\"w\"}]}");
    char tiny[16];
    CHECK_EQ_INT(registry_list(tiny, sizeof tiny, false), REG_ERR_SPACE);
}

static void parse_u32(void)
{
    uint32_t v = 0;
    CHECK(reg_parse_u32("0", 0, 10, &v) && v == 0);
    CHECK(reg_parse_u32("4294967295", 0, 0xFFFFFFFFu, &v) && v == 0xFFFFFFFFu);
    CHECK(!reg_parse_u32("4294967296", 0, 0xFFFFFFFFu, &v));
    CHECK(!reg_parse_u32("", 0, 10, &v));
    CHECK(!reg_parse_u32("-1", 0, 10, &v));
    CHECK(!reg_parse_u32(" 5", 0, 10, &v));
    CHECK(!reg_parse_u32("11", 0, 10, &v));
    CHECK(!reg_parse_u32("9", 10, 20, &v));
    CHECK(!reg_parse_u32(NULL, 0, 10, &v));
}

int main(void)
{
    RUN(get_set_roundtrip);
    RUN(errors);
    RUN(write_only_set);
    RUN(small_buffer);
    RUN(table_rules);
    RUN(list_keys);
    RUN(parse_u32);
    TEST_SUMMARY("test_registry");
}
