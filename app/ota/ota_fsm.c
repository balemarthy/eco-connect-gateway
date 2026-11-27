#include <string.h>

#include "ota_crc32.h"
#include "ota_fsm.h"

#define VERIFY_CHUNK  64U              /* stack buffer for reading the image back */

static const char *const state_names[OTA_STATE_COUNT] = {
    "idle", "requested", "downloading", "verifying", "ready", "failed"
};

static const char *const reason_names[OTA_R_COUNT] = {
    "none", "aborted", "empty image", "image too large", "storage error",
    "transport error", "size mismatch", "crc mismatch"
};

const char *ota_state_name(ota_state_t s)
{
    return ((unsigned)s < OTA_STATE_COUNT) ? state_names[s] : "?";
}

const char *ota_reason_name(ota_reason_t r)
{
    return ((unsigned)r < OTA_R_COUNT) ? reason_names[r] : "?";
}

void ota_fsm_init(ota_fsm_t *f, const ota_storage_t *st, uint32_t block_size, uint32_t max_size)
{
    memset(f, 0, sizeof *f);
    f->st = st;
    f->block_size = block_size;
    f->max_size = max_size;
    f->state = OTA_IDLE;
}

bool ota_fsm_active(const ota_fsm_t *f)
{
    return f->state == OTA_REQUESTED || f->state == OTA_DOWNLOADING || f->state == OTA_VERIFYING;
}

static void fail(ota_fsm_t *f, ota_reason_t reason)
{
    if (f->store_open) {
        (void)f->st->abort(f->st->ctx);
        f->store_open = false;
    }
    f->reason = reason;
    f->state = OTA_FAILED;
}

/* every block present, the store holds image_size bytes, CRC over the stored image */
static void verify(ota_fsm_t *f)
{
    uint8_t chunk[VERIFY_CHUNK];
    uint32_t crc = OTA_CRC32_INIT;
    uint32_t size = f->ra.image_size;

    if (!ota_reasm_complete(&f->ra) || ota_reasm_next_missing(&f->ra) >= 0) {
        fail(f, OTA_R_SIZE_MISMATCH);
        return;
    }
    for (uint32_t off = 0U; off < size; off += VERIFY_CHUNK) {
        size_t n = (size - off < VERIFY_CHUNK) ? (size_t)(size - off) : VERIFY_CHUNK;
        if (f->st->read(f->st->ctx, off, chunk, n) != OTA_ST_OK) {
            fail(f, OTA_R_STORAGE);
            return;
        }
        crc = ota_crc32_update(crc, chunk, n);
    }
    /* one byte past the end must be refused: the store holds exactly image_size bytes */
    if (f->st->read(f->st->ctx, size, chunk, 1U) == OTA_ST_OK) {
        fail(f, OTA_R_SIZE_MISMATCH);
        return;
    }
    f->computed_crc = ota_crc32_final(crc);
    if (f->computed_crc != f->expected_crc) {
        fail(f, OTA_R_CRC_MISMATCH);
        return;
    }
    f->state = OTA_READY;
}

static void on_block(ota_fsm_t *f, uint32_t index, const uint8_t *data, size_t len)
{
    f->last_block = ota_reasm_add(&f->ra, index, data, len);
    if (f->last_block == OTA_BLK_STORAGE) {
        fail(f, OTA_R_STORAGE);
        return;
    }
    /* bad index / length: counted in ra.rejected, the block is asked for again */
    if (ota_reasm_complete(&f->ra)) {
        f->store_open = false;
        if (f->st->close(f->st->ctx) != OTA_ST_OK) {
            (void)f->st->abort(f->st->ctx);
            f->reason = OTA_R_STORAGE;
            f->state = OTA_FAILED;
            return;
        }
        f->state = OTA_VERIFYING;
    }
}

static void on_begin(ota_fsm_t *f, uint32_t image_size)
{
    ota_ra_status_t rs = ota_reasm_init(&f->ra, f->st, image_size, f->block_size, f->max_size);
    if (rs == OTA_RA_EMPTY) {
        fail(f, OTA_R_EMPTY);
        return;
    }
    if (rs != OTA_RA_OK) {
        fail(f, OTA_R_TOO_LARGE);
        return;
    }
    if (f->st->open(f->st->ctx, image_size) != OTA_ST_OK) {
        fail(f, OTA_R_STORAGE);
        return;
    }
    f->store_open = true;
    f->state = OTA_DOWNLOADING;
}

ota_fsm_result_t ota_fsm_post(ota_fsm_t *f, const ota_event_t *ev)
{
    const ota_state_t s = f->state;
    const bool active = ota_fsm_active(f);

    switch (ev->type) {
    case OTA_EV_REQUEST:
        if (active) {
            return OTA_FSM_IGNORED;                  /* one download at a time */
        }
        memset(&f->ra, 0, sizeof f->ra);
        f->expected_crc = ev->u.request.crc32;
        f->computed_crc = 0U;
        f->reason = OTA_R_NONE;
        f->last_block = OTA_BLK_NEW;
        f->state = OTA_REQUESTED;
        return OTA_FSM_OK;

    case OTA_EV_BEGIN:
        if (s != OTA_REQUESTED) {
            return OTA_FSM_IGNORED;
        }
        on_begin(f, ev->u.begin.image_size);
        return OTA_FSM_OK;

    case OTA_EV_BLOCK:
        if (s != OTA_DOWNLOADING) {
            return OTA_FSM_IGNORED;
        }
        on_block(f, ev->u.block.index, ev->u.block.data, ev->u.block.len);
        return OTA_FSM_OK;

    case OTA_EV_VERIFY:
        if (s != OTA_VERIFYING) {
            return OTA_FSM_IGNORED;
        }
        verify(f);
        return OTA_FSM_OK;

    case OTA_EV_ERROR:
        if (!active) {
            return OTA_FSM_IGNORED;
        }
        fail(f, (ev->u.error.reason == OTA_R_NONE) ? OTA_R_TRANSPORT : ev->u.error.reason);
        return OTA_FSM_OK;

    case OTA_EV_ABORT:
        if (!active) {
            return OTA_FSM_IGNORED;
        }
        fail(f, OTA_R_ABORTED);
        return OTA_FSM_OK;

    case OTA_EV_RESET:
        if (active) {
            return OTA_FSM_IGNORED;                  /* abort first */
        }
        f->state = OTA_IDLE;
        f->reason = OTA_R_NONE;
        return OTA_FSM_OK;

    default:
        return OTA_FSM_IGNORED;
    }
}

ota_fsm_result_t ota_fsm_request(ota_fsm_t *f, uint32_t crc32)
{
    ota_event_t ev = { .type = OTA_EV_REQUEST };
    ev.u.request.crc32 = crc32;
    return ota_fsm_post(f, &ev);
}

ota_fsm_result_t ota_fsm_begin(ota_fsm_t *f, uint32_t image_size)
{
    ota_event_t ev = { .type = OTA_EV_BEGIN };
    ev.u.begin.image_size = image_size;
    return ota_fsm_post(f, &ev);
}

ota_fsm_result_t ota_fsm_block(ota_fsm_t *f, uint32_t index, const uint8_t *data, size_t len)
{
    ota_event_t ev = { .type = OTA_EV_BLOCK };
    ev.u.block.index = index;
    ev.u.block.data = data;
    ev.u.block.len = len;
    return ota_fsm_post(f, &ev);
}

ota_fsm_result_t ota_fsm_simple(ota_fsm_t *f, ota_ev_type_t type)
{
    ota_event_t ev = { .type = type };
    return ota_fsm_post(f, &ev);
}

ota_fsm_result_t ota_fsm_error(ota_fsm_t *f, ota_reason_t reason)
{
    ota_event_t ev = { .type = OTA_EV_ERROR };
    ev.u.error.reason = reason;
    return ota_fsm_post(f, &ev);
}
