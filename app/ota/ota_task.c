/*
 * OTA demo task (make DEMO=ota): downloads an image over HTTP Range requests into
 * the selected store and verifies size + CRC32. No flash write, no bootloader, no
 * bank swap: READY means "a verified image is stored".
 *
 * Commands (registry, so serial link and HTTP behave the same):
 *   SET ota_url http://192.168.1.10:8080/fw/eco-connect-gateway.bin
 *   SET ota start 1a2b3c4d        expected CRC32 of the whole file, 8 hex digits
 *   SET ota abort | SET ota reset
 *   GET ota                       {"state":"downloading","reason":"none","size":..,...}
 *   HTTP: POST /ota {"url":"...","crc32":"1a2b3c4d"} | {"cmd":"abort"}, GET /ota
 *
 * The setters run in the caller's task (proto / http): they only queue a command.
 * The OTA task owns the state machine and publishes a snapshot for GET ota.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#define LOG_TAG LOG_MOD_DEMO
#include "log.h"

#include "demo.h"
#include "net_init.h"
#include "ota_fsm.h"
#include "ota_http.h"
#include "ota_storage_fatfs.h"
#include "ota_storage_ram.h"
#include "registry.h"
#include "supervisor.h"

/* one block request: TCP connect (up to ~20 s to a silent host) + receive timeout */
#define OTA_SUP_WINDOW_MS   30000U
#define OTA_CMD_DEPTH       4U

typedef enum { OTA_CMD_START = 0, OTA_CMD_ABORT, OTA_CMD_RESET } ota_cmd_type_t;
typedef struct {
    uint8_t  type;
    uint32_t crc;
} ota_cmd_t;

typedef struct {                                  /* what GET ota shows */
    ota_state_t  state;
    ota_reason_t reason;
    uint32_t     size, blocks, received, dups, rejected, retries;
    uint32_t     crc_expected, crc;
} ota_snap_t;

static StaticTask_t task_tcb;
static StackType_t  task_stack[DEMO_TASK_STACK_WORDS];
static sup_id_t     sup_id = SUP_INVALID;

static StaticQueue_t q_ctrl;
static uint8_t       q_storage[OTA_CMD_DEPTH * sizeof(ota_cmd_t)];
static QueueHandle_t q;

static ota_fsm_t     fsm;
static ota_storage_t store;
#if OTA_STORAGE == OTA_STORAGE_FATFS
static ota_fatfs_store_t fat_store;
#else
static ota_ram_store_t ram_store;
static uint8_t         image_ram[OTA_MAX_IMAGE_SIZE];
#endif
static uint8_t       block_buf[OTA_BLOCK_SIZE];

static char          url_text[OTA_URL_MAX];      /* written by the ota_url setter */
static ota_snap_t    snap;
static uint32_t      retries_total;

/* ---------------------------------------------------------------- snapshot */
static void publish(void)
{
    ota_snap_t s = {
        .state = fsm.state, .reason = fsm.reason,
        .size = fsm.ra.image_size, .blocks = fsm.ra.n_blocks, .received = fsm.ra.received,
        .dups = fsm.ra.duplicates, .rejected = fsm.ra.rejected, .retries = retries_total,
        .crc_expected = fsm.expected_crc, .crc = fsm.computed_crc,
    };
    taskENTER_CRITICAL();
    snap = s;
    taskEXIT_CRITICAL();
}

/* ---------------------------------------------------------------- download */
static bool abort_requested(void)
{
    ota_cmd_t c;
    while (xQueueReceive(q, &c, 0) == pdPASS) {
        if (c.type == OTA_CMD_ABORT) {
            return true;
        }
        LOG_W("ota: busy, command %u ignored", (unsigned)c.type);
    }
    return false;
}

/* GET one range with retries. On success body is in block_buf. */
static bool fetch(const ota_url_t *u, uint32_t first, uint32_t last, size_t *len, ota_http_head_t *h)
{
    for (uint32_t attempt = 0U; attempt < OTA_BLOCK_RETRIES; attempt++) {
        memset(h, 0, sizeof *h);
        sup_checkin(sup_id);
        ota_get_status_t st = ota_http_get_range(u, first, last, block_buf, sizeof block_buf, len, h);
        sup_checkin(sup_id);
        if (st == OTA_GET_OK) {
            return true;
        }
        retries_total++;
        LOG_W("ota: bytes %lu-%lu: %s (status %d), try %lu", (unsigned long)first,
              (unsigned long)last, ota_get_status_str(st), h->status, (unsigned long)(attempt + 1U));
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
    return false;
}

static void download(const ota_url_t *u)
{
    ota_http_head_t h;
    size_t len = 0U;

    /* block 0 also tells the total size (Content-Range) */
    if (!fetch(u, 0U, OTA_BLOCK_SIZE - 1U, &len, &h)) {
        (void)ota_fsm_error(&fsm, OTA_R_TRANSPORT);
        return;
    }
    uint32_t total;
    if (h.status == 206 && h.has_range && h.range_first == 0U) {
        total = h.range_total;
    } else if (h.status == 200 && len <= OTA_BLOCK_SIZE) {
        total = (uint32_t)len;                       /* no Range support, but the file fits one block */
    } else {
        LOG_E("ota: server does not answer Range requests");
        (void)ota_fsm_error(&fsm, OTA_R_TRANSPORT);
        return;
    }
    (void)ota_fsm_begin(&fsm, total);
    publish();
    if (fsm.state != OTA_DOWNLOADING) {
        return;                                      /* too large / empty / store error */
    }
    LOG_I("ota: %lu bytes, %lu blocks of %u", (unsigned long)total, (unsigned long)fsm.ra.n_blocks,
          (unsigned)OTA_BLOCK_SIZE);
    (void)ota_fsm_block(&fsm, 0U, block_buf, len);
    publish();

    uint32_t bad = 0U;
    while (fsm.state == OTA_DOWNLOADING) {
        if (abort_requested()) {
            (void)ota_fsm_simple(&fsm, OTA_EV_ABORT);
            break;
        }
        int32_t idx = ota_reasm_next_missing(&fsm.ra);
        if (idx < 0) {
            break;
        }
        uint32_t first = (uint32_t)idx * OTA_BLOCK_SIZE;
        uint32_t last = first + ota_reasm_block_len(&fsm.ra, (uint32_t)idx) - 1U;
        if (!fetch(u, first, last, &len, &h)) {
            (void)ota_fsm_error(&fsm, OTA_R_TRANSPORT);
            break;
        }
        /* a reply for another range is passed on as a bad block (the length rule rejects it) */
        uint32_t at = (h.has_range && h.range_first % OTA_BLOCK_SIZE == 0U) ?
                      h.range_first / OTA_BLOCK_SIZE : (uint32_t)idx;
        (void)ota_fsm_block(&fsm, at, block_buf, len);
        if (fsm.last_block == OTA_BLK_BAD_INDEX || fsm.last_block == OTA_BLK_BAD_LENGTH ||
            fsm.last_block == OTA_BLK_DUPLICATE) {
            if (++bad > OTA_BLOCK_RETRIES * 2U) {
                (void)ota_fsm_error(&fsm, OTA_R_TRANSPORT);
            }
        }
        publish();
    }
    if (fsm.state == OTA_VERIFYING) {
        (void)ota_fsm_simple(&fsm, OTA_EV_VERIFY);
    }
}

static void start(uint32_t crc)
{
    char text[OTA_URL_MAX];
    ota_url_t u;

    taskENTER_CRITICAL();
    memcpy(text, url_text, sizeof text);
    taskEXIT_CRITICAL();

    if (ota_fsm_request(&fsm, crc) != OTA_FSM_OK) {
        LOG_W("ota: start ignored in state %s", ota_state_name(fsm.state));
        return;
    }
    retries_total = 0U;
    publish();
    LOG_I("ota: requested %s crc %08lx", text, (unsigned long)crc);
    if (!net_is_up() || !ota_url_parse(text, &u)) {
        (void)ota_fsm_error(&fsm, OTA_R_TRANSPORT);
    } else {
        download(&u);
    }
    publish();
    if (fsm.state == OTA_READY) {
        LOG_I("ota: image ready, %lu bytes, crc %08lx (stored in %s, not installed)",
              (unsigned long)fsm.ra.image_size, (unsigned long)fsm.computed_crc, store.name);
    } else {
        LOG_W("ota: failed: %s", ota_reason_name(fsm.reason));
    }
}

static void ota_task(void *arg)
{
    (void)arg;
    publish();
    for (;;) {
        ota_cmd_t c;
        BaseType_t got = xQueueReceive(q, &c, pdMS_TO_TICKS(1000U));
        sup_checkin(sup_id);
        if (got != pdPASS) {
            continue;
        }
        switch (c.type) {
        case OTA_CMD_START:
            start(c.crc);
            break;
        case OTA_CMD_ABORT:
            (void)ota_fsm_simple(&fsm, OTA_EV_ABORT);  /* not active: ignored */
            break;
        default:
            (void)ota_fsm_simple(&fsm, OTA_EV_RESET);
            break;
        }
        publish();
    }
}

/* ---------------------------------------------------------------- registry */
static bool parse_hex32(const char *s, uint32_t *out)
{
    uint32_t v = 0U;
    unsigned n = 0U;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    for (; *s != '\0'; s++, n++) {
        char ch = *s;
        uint32_t d;
        if (ch >= '0' && ch <= '9') {
            d = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            d = (uint32_t)(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            d = (uint32_t)(ch - 'A' + 10);
        } else {
            return false;
        }
        if (n >= 8U) {
            return false;
        }
        v = (v << 4) | d;
    }
    *out = v;
    return n > 0U;
}

static void hex8(char *buf, size_t size, uint32_t v)
{
    (void)snprintf(buf, size, "%08lx", (unsigned long)v);
}

static reg_status_t get_ota(json_writer_t *w, void *ctx)
{
    (void)ctx;
    ota_snap_t s;
    char a[9], b[9];
    taskENTER_CRITICAL();
    s = snap;
    taskEXIT_CRITICAL();
    hex8(a, sizeof a, s.crc_expected);
    hex8(b, sizeof b, s.crc);
    jw_kv_str(w, "state", ota_state_name(s.state));
    jw_kv_str(w, "reason", ota_reason_name(s.reason));
    jw_kv_uint(w, "size", s.size);
    jw_kv_uint(w, "blocks", s.blocks);
    jw_kv_uint(w, "received", s.received);
    jw_kv_uint(w, "dups", s.dups);
    jw_kv_uint(w, "rejected", s.rejected);
    jw_kv_uint(w, "retries", s.retries);
    jw_kv_str(w, "crc_expected", a);
    jw_kv_str(w, "crc", b);
    return REG_OK;
}

/* "start <crc32 hex>" | "abort" | "reset" */
static reg_status_t set_ota(const char *v, void *ctx)
{
    (void)ctx;
    ota_cmd_t c = { .type = OTA_CMD_RESET, .crc = 0U };
    if (strncmp(v, "start ", 6) == 0) {
        if (!parse_hex32(v + 6, &c.crc)) {
            return REG_ERR_VALUE;
        }
        c.type = OTA_CMD_START;
    } else if (strcmp(v, "abort") == 0) {
        c.type = OTA_CMD_ABORT;
    } else if (strcmp(v, "reset") != 0) {
        return REG_ERR_VALUE;
    }
    return (xQueueSend(q, &c, 0) == pdPASS) ? REG_OK : REG_ERR_VALUE;
}

static reg_status_t get_ota_url(json_writer_t *w, void *ctx)
{
    (void)ctx;
    char text[OTA_URL_MAX];
    taskENTER_CRITICAL();
    memcpy(text, url_text, sizeof text);
    taskEXIT_CRITICAL();
    jw_kv_str(w, "ota_url", text);
    return REG_OK;
}

static reg_status_t set_ota_url(const char *v, void *ctx)
{
    (void)ctx;
    ota_url_t u;
    size_t n = strlen(v);
    if (n >= sizeof url_text || !ota_url_parse(v, &u)) {
        return REG_ERR_VALUE;
    }
    taskENTER_CRITICAL();
    memcpy(url_text, v, n + 1U);                   /* the task copies it again at start */
    taskEXIT_CRITICAL();
    return REG_OK;
}

static const reg_entry_t entries[] = {
    { "ota",     "OTA: start <crc32hex> | abort | reset", get_ota,     set_ota,     NULL },
    { "ota_url", "OTA image URL http://<ipv4>[:port]/path", get_ota_url, set_ota_url, NULL },
};

void demo_start(void)
{
    (void)snprintf(url_text, sizeof url_text, "http://%s:%u%s", OTA_SERVER_HOST,
                   (unsigned)OTA_SERVER_PORT, OTA_SERVER_PATH);
#if OTA_STORAGE == OTA_STORAGE_FATFS
    ota_storage_fatfs_init(&store, &fat_store);
#else
    ota_storage_ram_init(&store, &ram_store, image_ram, (uint32_t)sizeof image_ram);
#endif
    ota_fsm_init(&fsm, &store, OTA_BLOCK_SIZE, OTA_IMAGE_LIMIT);

    q = xQueueCreateStatic(OTA_CMD_DEPTH, sizeof(ota_cmd_t), q_storage, &q_ctrl);
    configASSERT(q != NULL);
    for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; i++) {
        if (registry_register(&entries[i]) != REG_OK) {
            LOG_E("ota: cannot register %s", entries[i].key);
        }
    }
    sup_id = sup_register("ota", OTA_SUP_WINDOW_MS);
    TaskHandle_t h = xTaskCreateStatic(ota_task, "ota", DEMO_TASK_STACK_WORDS, NULL,
                                       tskIDLE_PRIORITY + DEMO_TASK_PRIORITY, task_stack, &task_tcb);
    configASSERT(h != NULL);
}
