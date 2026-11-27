/*
 * Host tests: OTA CRC32, block reassembly, RAM store and download state machine.
 */
#include <stdint.h>
#include <string.h>

#include "ota_crc32.h"
#include "ota_fsm.h"
#include "ota_reasm.h"
#include "ota_storage_ram.h"
#include "test_util.h"

#define BS        16U                         /* small blocks keep the test images short */
#define CAP       ((uint32_t)(OTA_REASM_MAX_BLOCKS * BS))

static uint8_t       ram_buf[CAP];
static ota_ram_store_t ram;
static ota_storage_t store;

static uint8_t image[CAP];

static void make_image(uint32_t size)
{
    for (uint32_t i = 0U; i < size; i++) {
        image[i] = (uint8_t)(i * 7U + 3U);
    }
}

static void fresh_store(void)
{
    ota_storage_ram_init(&store, &ram, ram_buf, (uint32_t)sizeof ram_buf);
}

/* ------------------------------------------------------------------ crc32 */
static void test_crc_known_values(void)
{
    const uint8_t check[] = "123456789";
    CHECK_EQ_INT(ota_crc32(check, 9U), 0xCBF43926UL);
    CHECK_EQ_INT(ota_crc32(NULL, 0U), 0x00000000UL);
    const uint8_t a[] = "a";
    CHECK_EQ_INT(ota_crc32(a, 1U), 0xE8B7BE43UL);
    const uint8_t fox[] = "The quick brown fox jumps over the lazy dog";
    CHECK_EQ_INT(ota_crc32(fox, 43U), 0x414FA339UL);
    uint8_t zeros[32];
    memset(zeros, 0, sizeof zeros);
    CHECK_EQ_INT(ota_crc32(zeros, sizeof zeros), 0x190A55ADUL);
}

static void test_crc_incremental(void)
{
    make_image(200U);
    uint32_t one = ota_crc32(image, 200U);
    uint32_t c = OTA_CRC32_INIT;
    c = ota_crc32_update(c, image, 1U);
    c = ota_crc32_update(c, image + 1, 99U);
    c = ota_crc32_update(c, image + 100, 0U);
    c = ota_crc32_update(c, image + 100, 100U);
    CHECK_EQ_INT(ota_crc32_final(c), one);
    image[57] ^= 0x01U;                              /* one bit flipped -> different CRC */
    CHECK(ota_crc32(image, 200U) != one);
}

/* ------------------------------------------------------------------ RAM store */
static void test_ram_store(void)
{
    uint8_t b[4] = { 1, 2, 3, 4 };
    uint8_t r[4];
    fresh_store();
    CHECK_EQ_INT(store.capacity(store.ctx), CAP);
    CHECK_EQ_INT(store.write(store.ctx, 0U, b, 4U), OTA_ST_ERR_STATE);   /* not open */
    CHECK_EQ_INT(store.open(store.ctx, 0U), OTA_ST_ERR_SIZE);
    CHECK_EQ_INT(store.open(store.ctx, CAP + 1U), OTA_ST_ERR_SIZE);
    CHECK_EQ_INT(store.open(store.ctx, 10U), OTA_ST_OK);
    CHECK_EQ_INT(store.write(store.ctx, 6U, b, 4U), OTA_ST_OK);          /* 6..9: fits */
    CHECK_EQ_INT(store.write(store.ctx, 7U, b, 4U), OTA_ST_ERR_SIZE);    /* 7..10: past the end */
    CHECK_EQ_INT(store.write(store.ctx, 0xFFFFFFFFUL, b, 4U), OTA_ST_ERR_SIZE);
    CHECK_EQ_INT(store.read(store.ctx, 6U, r, 4U), OTA_ST_OK);
    CHECK(memcmp(r, b, 4U) == 0);
    CHECK_EQ_INT(store.read(store.ctx, 0U, r, 1U), OTA_ST_OK);
    CHECK_EQ_INT(r[0], 0xFF);                                            /* unwritten = erased */
    CHECK_EQ_INT(store.close(store.ctx), OTA_ST_OK);
    CHECK_EQ_INT(store.read(store.ctx, 6U, r, 4U), OTA_ST_OK);           /* kept after close */
    CHECK_EQ_INT(store.write(store.ctx, 0U, b, 1U), OTA_ST_ERR_STATE);   /* closed */
    CHECK_EQ_INT(store.abort(store.ctx), OTA_ST_OK);
    CHECK_EQ_INT(store.read(store.ctx, 6U, r, 4U), OTA_ST_ERR_STATE);
}

/* ------------------------------------------------------------------ reassembly */
static void test_reasm_limits(void)
{
    ota_reasm_t r;
    fresh_store();
    CHECK_EQ_INT(ota_reasm_init(&r, &store, 100U, 0U, CAP), OTA_RA_BAD_BLOCK_SIZE);
    CHECK_EQ_INT(ota_reasm_init(&r, &store, 0U, BS, CAP), OTA_RA_EMPTY);
    CHECK_EQ_INT(ota_reasm_init(&r, &store, 101U, BS, 100U), OTA_RA_TOO_LARGE);   /* max_size */
    CHECK_EQ_INT(ota_reasm_init(&r, &store, CAP + 1U, BS, 0xFFFFFFFFUL), OTA_RA_TOO_LARGE); /* capacity */
    CHECK_EQ_INT(ota_reasm_init(&r, &store, CAP, 1U, 0xFFFFFFFFUL), OTA_RA_TOO_LARGE); /* bitmap */
    CHECK_EQ_INT(ota_reasm_init(&r, &store, CAP, BS, CAP), OTA_RA_OK);            /* exactly full */
    CHECK_EQ_INT(r.n_blocks, OTA_REASM_MAX_BLOCKS);
    CHECK_EQ_INT(ota_reasm_init(&r, &store, 100U, BS, CAP), OTA_RA_OK);
    CHECK_EQ_INT(r.n_blocks, 7U);                    /* 6 x 16 + 4 */
    CHECK_EQ_INT(ota_reasm_block_len(&r, 0U), BS);
    CHECK_EQ_INT(ota_reasm_block_len(&r, 5U), BS);
    CHECK_EQ_INT(ota_reasm_block_len(&r, 6U), 4U);
    CHECK_EQ_INT(ota_reasm_block_len(&r, 7U), 0U);
    CHECK_EQ_INT(ota_reasm_init(&r, &store, 96U, BS, CAP), OTA_RA_OK);
    CHECK_EQ_INT(r.n_blocks, 6U);                    /* exact multiple: last block full */
    CHECK_EQ_INT(ota_reasm_block_len(&r, 5U), BS);
}

static void test_reasm_out_of_order_and_duplicates(void)
{
    ota_reasm_t r;
    const uint32_t size = 100U;
    const uint32_t order[] = { 6U, 2U, 0U, 2U, 5U, 1U, 6U, 3U, 4U };
    make_image(size);
    fresh_store();
    CHECK_EQ_INT(store.open(store.ctx, size), OTA_ST_OK);
    CHECK_EQ_INT(ota_reasm_init(&r, &store, size, BS, CAP), OTA_RA_OK);
    CHECK_EQ_INT(ota_reasm_next_missing(&r), 0);

    int news = 0, dups = 0;
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        uint32_t b = order[i];
        ota_blk_status_t st = ota_reasm_add(&r, b, &image[b * BS], ota_reasm_block_len(&r, b));
        if (st == OTA_BLK_NEW) {
            news++;
        } else if (st == OTA_BLK_DUPLICATE) {
            dups++;
        }
        if (i == 2U) {
            CHECK_EQ_INT(ota_reasm_next_missing(&r), 1);   /* have 0, 2, 6 */
            CHECK(!ota_reasm_complete(&r));
        }
    }
    CHECK_EQ_INT(news, 7);
    CHECK_EQ_INT(dups, 2);
    CHECK_EQ_INT(r.duplicates, 2U);
    CHECK_EQ_INT(r.received, 7U);
    CHECK(ota_reasm_complete(&r));
    CHECK_EQ_INT(ota_reasm_next_missing(&r), -1);
    CHECK(memcmp(ram_buf, image, size) == 0);
}

static void test_reasm_rejects(void)
{
    ota_reasm_t r;
    make_image(100U);
    fresh_store();
    CHECK_EQ_INT(store.open(store.ctx, 100U), OTA_ST_OK);
    CHECK_EQ_INT(ota_reasm_init(&r, &store, 100U, BS, CAP), OTA_RA_OK);
    CHECK_EQ_INT(ota_reasm_add(&r, 7U, image, 4U), OTA_BLK_BAD_INDEX);
    CHECK_EQ_INT(ota_reasm_add(&r, 0xFFFFFFFFUL, image, BS), OTA_BLK_BAD_INDEX);
    CHECK_EQ_INT(ota_reasm_add(&r, 0U, image, BS - 1U), OTA_BLK_BAD_LENGTH);  /* short */
    CHECK_EQ_INT(ota_reasm_add(&r, 0U, image, BS + 1U), OTA_BLK_BAD_LENGTH);  /* long */
    CHECK_EQ_INT(ota_reasm_add(&r, 6U, image, BS), OTA_BLK_BAD_LENGTH);       /* last is 4 */
    CHECK_EQ_INT(ota_reasm_add(&r, 1U, NULL, BS), OTA_BLK_BAD_LENGTH);
    CHECK_EQ_INT(r.rejected, 6U);
    CHECK_EQ_INT(r.received, 0U);
    CHECK_EQ_INT(ota_reasm_add(&r, 6U, &image[96], 4U), OTA_BLK_NEW);
    CHECK(ota_reasm_have(&r, 6U));
    CHECK(!ota_reasm_have(&r, 5U));
    CHECK(!ota_reasm_have(&r, 99U));
}

/* ------------------------------------------------------------------ store that fails on demand */
static int fail_write_at = -1;      /* write number that fails, -1 never */
static int fail_close;
static int writes, aborts;

static ota_st_status_t f_open(void *c, uint32_t s) { (void)c; return (s <= CAP) ? OTA_ST_OK : OTA_ST_ERR_SIZE; }
static ota_st_status_t f_write(void *c, uint32_t o, const uint8_t *d, size_t n)
{
    (void)c;
    if (writes++ == fail_write_at) {
        return OTA_ST_ERR_IO;
    }
    memcpy(&ram_buf[o], d, n);
    return OTA_ST_OK;
}
static ota_st_status_t f_read(void *c, uint32_t o, uint8_t *d, size_t n) { (void)c; (void)o; (void)d; (void)n; return OTA_ST_ERR_IO; }
static ota_st_status_t f_close(void *c) { (void)c; return fail_close ? OTA_ST_ERR_IO : OTA_ST_OK; }
static ota_st_status_t f_abort(void *c) { (void)c; aborts++; return OTA_ST_OK; }
static uint32_t f_cap(void *c) { (void)c; return CAP; }

static const ota_storage_t failing = { "fail", f_open, f_write, f_read, f_close, f_abort, f_cap, NULL };

/* ------------------------------------------------------------------ state machine */
static void feed_all(ota_fsm_t *f, uint32_t size, int reverse)
{
    uint32_t n = (size + BS - 1U) / BS;
    for (uint32_t k = 0U; k < n; k++) {
        uint32_t b = reverse ? (n - 1U - k) : k;
        uint32_t len = (b + 1U < n) ? BS : size - b * BS;
        (void)ota_fsm_block(f, b, &image[b * BS], len);
    }
}

static void test_fsm_happy_path(void)
{
    ota_fsm_t f;
    const uint32_t size = 100U;
    make_image(size);
    fresh_store();
    ota_fsm_init(&f, &store, BS, CAP);
    CHECK_EQ_INT(f.state, OTA_IDLE);
    CHECK_EQ_STR(ota_state_name(f.state), "idle");
    CHECK(!ota_fsm_active(&f));

    CHECK_EQ_INT(ota_fsm_request(&f, ota_crc32(image, size)), OTA_FSM_OK);
    CHECK_EQ_INT(f.state, OTA_REQUESTED);
    CHECK(ota_fsm_active(&f));
    CHECK_EQ_INT(ota_fsm_block(&f, 0U, image, BS), OTA_FSM_IGNORED);     /* no BEGIN yet */
    CHECK_EQ_INT(ota_fsm_begin(&f, size), OTA_FSM_OK);
    CHECK_EQ_INT(f.state, OTA_DOWNLOADING);
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_VERIFY), OTA_FSM_IGNORED);    /* not complete */
    feed_all(&f, size, 1);
    CHECK_EQ_INT(f.state, OTA_VERIFYING);
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_VERIFY), OTA_FSM_OK);
    CHECK_EQ_INT(f.state, OTA_READY);
    CHECK_EQ_INT(f.computed_crc, ota_crc32(image, size));
    CHECK_EQ_INT(f.reason, OTA_R_NONE);
    CHECK(memcmp(ram_buf, image, size) == 0);

    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_RESET), OTA_FSM_OK);
    CHECK_EQ_INT(f.state, OTA_IDLE);
}

static void test_fsm_single_block_and_exact_fit(void)
{
    ota_fsm_t f;
    make_image(CAP);
    fresh_store();
    ota_fsm_init(&f, &store, BS, CAP);
    (void)ota_fsm_request(&f, ota_crc32(image, 5U));
    (void)ota_fsm_begin(&f, 5U);
    (void)ota_fsm_block(&f, 0U, image, 5U);
    CHECK_EQ_INT(f.state, OTA_VERIFYING);
    (void)ota_fsm_simple(&f, OTA_EV_VERIFY);
    CHECK_EQ_INT(f.state, OTA_READY);

    (void)ota_fsm_request(&f, ota_crc32(image, CAP));                    /* READY -> new request */
    CHECK_EQ_INT(f.state, OTA_REQUESTED);
    (void)ota_fsm_begin(&f, CAP);
    feed_all(&f, CAP, 0);
    (void)ota_fsm_simple(&f, OTA_EV_VERIFY);
    CHECK_EQ_INT(f.state, OTA_READY);
}

static void test_fsm_duplicates_and_bad_blocks(void)
{
    ota_fsm_t f;
    make_image(40U);
    fresh_store();
    ota_fsm_init(&f, &store, BS, CAP);
    (void)ota_fsm_request(&f, ota_crc32(image, 40U));
    (void)ota_fsm_begin(&f, 40U);                    /* blocks 16, 16, 8 */
    (void)ota_fsm_block(&f, 1U, &image[16], BS);
    (void)ota_fsm_block(&f, 1U, &image[16], BS);
    CHECK_EQ_INT(f.last_block, OTA_BLK_DUPLICATE);
    (void)ota_fsm_block(&f, 2U, &image[32], BS);     /* last block too long */
    CHECK_EQ_INT(f.last_block, OTA_BLK_BAD_LENGTH);
    (void)ota_fsm_block(&f, 3U, image, 8U);
    CHECK_EQ_INT(f.last_block, OTA_BLK_BAD_INDEX);
    CHECK_EQ_INT(f.state, OTA_DOWNLOADING);          /* bad blocks do not fail the download */
    (void)ota_fsm_block(&f, 2U, &image[32], 8U);
    (void)ota_fsm_block(&f, 0U, image, BS);
    CHECK_EQ_INT(f.state, OTA_VERIFYING);
    CHECK_EQ_INT(f.ra.duplicates, 1U);
    CHECK_EQ_INT(f.ra.rejected, 2U);
    (void)ota_fsm_simple(&f, OTA_EV_VERIFY);
    CHECK_EQ_INT(f.state, OTA_READY);
}

static void test_fsm_failures(void)
{
    ota_fsm_t f;
    make_image(100U);

    /* wrong CRC */
    fresh_store();
    ota_fsm_init(&f, &store, BS, CAP);
    (void)ota_fsm_request(&f, ota_crc32(image, 100U) ^ 1U);
    (void)ota_fsm_begin(&f, 100U);
    feed_all(&f, 100U, 0);
    (void)ota_fsm_simple(&f, OTA_EV_VERIFY);
    CHECK_EQ_INT(f.state, OTA_FAILED);
    CHECK_EQ_INT(f.reason, OTA_R_CRC_MISMATCH);
    CHECK_EQ_STR(ota_reason_name(f.reason), "crc mismatch");

    /* corrupted block content -> CRC catches it */
    fresh_store();
    ota_fsm_init(&f, &store, BS, CAP);
    (void)ota_fsm_request(&f, ota_crc32(image, 100U));
    (void)ota_fsm_begin(&f, 100U);
    image[20] ^= 0x80U;
    feed_all(&f, 100U, 0);
    image[20] ^= 0x80U;
    (void)ota_fsm_simple(&f, OTA_EV_VERIFY);
    CHECK_EQ_INT(f.reason, OTA_R_CRC_MISMATCH);

    /* too large, empty */
    ota_fsm_init(&f, &store, BS, 64U);
    (void)ota_fsm_request(&f, 0U);
    (void)ota_fsm_begin(&f, 65U);
    CHECK_EQ_INT(f.state, OTA_FAILED);
    CHECK_EQ_INT(f.reason, OTA_R_TOO_LARGE);
    (void)ota_fsm_request(&f, 0U);                   /* FAILED -> REQUESTED again */
    (void)ota_fsm_begin(&f, 0U);
    CHECK_EQ_INT(f.reason, OTA_R_EMPTY);

    /* abort while downloading: store dropped, later blocks ignored */
    aborts = 0;
    writes = 0;
    fail_write_at = -1;
    fail_close = 0;
    ota_fsm_init(&f, &failing, BS, CAP);
    (void)ota_fsm_request(&f, 0U);
    (void)ota_fsm_begin(&f, 100U);
    (void)ota_fsm_block(&f, 0U, image, BS);
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_ABORT), OTA_FSM_OK);
    CHECK_EQ_INT(f.state, OTA_FAILED);
    CHECK_EQ_INT(f.reason, OTA_R_ABORTED);
    CHECK_EQ_INT(aborts, 1);
    CHECK_EQ_INT(ota_fsm_block(&f, 1U, &image[16], BS), OTA_FSM_IGNORED);
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_ABORT), OTA_FSM_IGNORED);

    /* storage write error */
    writes = 0;
    fail_write_at = 2;
    ota_fsm_init(&f, &failing, BS, CAP);
    (void)ota_fsm_request(&f, 0U);
    (void)ota_fsm_begin(&f, 100U);
    feed_all(&f, 100U, 0);
    CHECK_EQ_INT(f.state, OTA_FAILED);
    CHECK_EQ_INT(f.reason, OTA_R_STORAGE);
    CHECK_EQ_INT(f.ra.received, 2U);

    /* close error, then read-back error during verification */
    writes = 0;
    fail_write_at = -1;
    fail_close = 1;
    ota_fsm_init(&f, &failing, BS, CAP);
    (void)ota_fsm_request(&f, 0U);
    (void)ota_fsm_begin(&f, 20U);
    feed_all(&f, 20U, 0);
    CHECK_EQ_INT(f.reason, OTA_R_STORAGE);
    fail_close = 0;
    (void)ota_fsm_request(&f, 0U);
    (void)ota_fsm_begin(&f, 20U);
    feed_all(&f, 20U, 0);
    CHECK_EQ_INT(f.state, OTA_VERIFYING);
    (void)ota_fsm_simple(&f, OTA_EV_VERIFY);         /* f_read always fails */
    CHECK_EQ_INT(f.reason, OTA_R_STORAGE);

    /* transport error; ERROR with reason NONE counts as transport */
    fresh_store();
    ota_fsm_init(&f, &store, BS, CAP);
    CHECK_EQ_INT(ota_fsm_error(&f, OTA_R_TRANSPORT), OTA_FSM_IGNORED);   /* idle */
    (void)ota_fsm_request(&f, 0U);
    CHECK_EQ_INT(ota_fsm_error(&f, OTA_R_NONE), OTA_FSM_OK);
    CHECK_EQ_INT(f.reason, OTA_R_TRANSPORT);
}

static void test_fsm_ignored_events(void)
{
    ota_fsm_t f;
    fresh_store();
    ota_fsm_init(&f, &store, BS, CAP);
    CHECK_EQ_INT(ota_fsm_begin(&f, 10U), OTA_FSM_IGNORED);
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_VERIFY), OTA_FSM_IGNORED);
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_ABORT), OTA_FSM_IGNORED);
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_RESET), OTA_FSM_OK);          /* idle -> idle */
    (void)ota_fsm_request(&f, 0U);
    CHECK_EQ_INT(ota_fsm_request(&f, 0U), OTA_FSM_IGNORED);              /* one at a time */
    CHECK_EQ_INT(ota_fsm_simple(&f, OTA_EV_RESET), OTA_FSM_IGNORED);     /* abort first */
    (void)ota_fsm_begin(&f, 10U);
    CHECK_EQ_INT(ota_fsm_begin(&f, 10U), OTA_FSM_IGNORED);
    ota_event_t bad = { .type = (ota_ev_type_t)99 };
    CHECK_EQ_INT(ota_fsm_post(&f, &bad), OTA_FSM_IGNORED);
    CHECK_EQ_INT(f.state, OTA_DOWNLOADING);
    CHECK_EQ_STR(ota_state_name((ota_state_t)42), "?");
    CHECK_EQ_STR(ota_reason_name((ota_reason_t)42), "?");
}

int main(void)
{
    RUN(test_crc_known_values);
    RUN(test_crc_incremental);
    RUN(test_ram_store);
    RUN(test_reasm_limits);
    RUN(test_reasm_out_of_order_and_duplicates);
    RUN(test_reasm_rejects);
    RUN(test_fsm_happy_path);
    RUN(test_fsm_single_block_and_exact_fit);
    RUN(test_fsm_duplicates_and_bad_blocks);
    RUN(test_fsm_failures);
    RUN(test_fsm_ignored_events);
    TEST_SUMMARY("test_ota");
}
