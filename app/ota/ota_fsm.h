/*
 * OTA download state machine: event driven, no RTOS, no I/O of its own.
 *
 *            REQUEST(crc)          BEGIN(size)             last BLOCK            VERIFY
 *   IDLE ---------------> REQUESTED -----------> DOWNLOADING -----------> VERIFYING -------> READY
 *    ^                        |                     |  BLOCK (any order,       |
 *    |                        |                     |  duplicates ignored)     | size/CRC wrong
 *    |   RESET                v ERROR / ABORT       v ERROR / ABORT / storage  v
 *    +------------------- FAILED <---------------------------------------------+
 *   READY / FAILED: REQUEST starts again, RESET goes back to IDLE.
 *
 * The transport (HTTP Range on the target, the test code on the host) feeds events;
 * the image goes into an ota_storage_t backend; verification = every block present,
 * stored size == announced size, CRC32 over the stored bytes == the CRC given with
 * REQUEST. Events not valid in the current state are ignored (OTA_FSM_IGNORED) and
 * change nothing, so a late block after an abort is harmless.
 */
#ifndef OTA_FSM_H
#define OTA_FSM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_reasm.h"
#include "ota_storage.h"

typedef enum {
    OTA_IDLE = 0,
    OTA_REQUESTED,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_READY,
    OTA_FAILED,
    OTA_STATE_COUNT
} ota_state_t;

typedef enum {
    OTA_EV_REQUEST = 0,      /* u.request.crc32: expected CRC of the whole image */
    OTA_EV_BEGIN,            /* u.begin.image_size: total size, e.g. from Content-Range */
    OTA_EV_BLOCK,            /* u.block: index, data, len */
    OTA_EV_VERIFY,           /* run the checks (state VERIFYING) */
    OTA_EV_ERROR,            /* u.error.reason: transport or caller error */
    OTA_EV_ABORT,
    OTA_EV_RESET
} ota_ev_type_t;

typedef enum {
    OTA_R_NONE = 0,
    OTA_R_ABORTED,
    OTA_R_EMPTY,             /* announced size 0 */
    OTA_R_TOO_LARGE,         /* over OTA_MAX_IMAGE_SIZE / store capacity */
    OTA_R_STORAGE,           /* store open/write/read/close failed */
    OTA_R_TRANSPORT,         /* server unreachable, bad response, retries used up */
    OTA_R_SIZE_MISMATCH,
    OTA_R_CRC_MISMATCH,
    OTA_R_COUNT
} ota_reason_t;

typedef struct {
    ota_ev_type_t type;
    union {
        struct { uint32_t crc32; } request;
        struct { uint32_t image_size; } begin;
        struct { uint32_t index; const uint8_t *data; size_t len; } block;
        struct { ota_reason_t reason; } error;
    } u;
} ota_event_t;

typedef enum {
    OTA_FSM_OK = 0,          /* event handled (the state may or may not change) */
    OTA_FSM_IGNORED          /* not valid in this state */
} ota_fsm_result_t;

typedef struct {
    ota_state_t   state;
    ota_reason_t  reason;          /* why FAILED */
    uint32_t      expected_crc;
    uint32_t      computed_crc;    /* valid after VERIFY */
    uint32_t      block_size;
    uint32_t      max_size;
    bool          store_open;
    ota_blk_status_t last_block;   /* result of the last BLOCK event */
    const ota_storage_t *st;
    ota_reasm_t   ra;
} ota_fsm_t;

void ota_fsm_init(ota_fsm_t *f, const ota_storage_t *st, uint32_t block_size, uint32_t max_size);
ota_fsm_result_t ota_fsm_post(ota_fsm_t *f, const ota_event_t *ev);

/* convenience wrappers around ota_fsm_post() */
ota_fsm_result_t ota_fsm_request(ota_fsm_t *f, uint32_t crc32);
ota_fsm_result_t ota_fsm_begin(ota_fsm_t *f, uint32_t image_size);
ota_fsm_result_t ota_fsm_block(ota_fsm_t *f, uint32_t index, const uint8_t *data, size_t len);
ota_fsm_result_t ota_fsm_simple(ota_fsm_t *f, ota_ev_type_t type);          /* VERIFY, ABORT, RESET */
ota_fsm_result_t ota_fsm_error(ota_fsm_t *f, ota_reason_t reason);

bool        ota_fsm_active(const ota_fsm_t *f);    /* REQUESTED, DOWNLOADING or VERIFYING */
const char *ota_state_name(ota_state_t s);
const char *ota_reason_name(ota_reason_t r);

#endif /* OTA_FSM_H */
