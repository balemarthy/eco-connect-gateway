/*
 * Storage task: readings -> JSON lines in READINGS.LOG, plus a RAM tail, and
 * storage_call() jobs from other tasks (the OTA FatFs store). One queue carries both,
 * so the task blocks in one place and stays the only caller of FatFs.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "ff.h"

#define LOG_TAG LOG_MOD_FS
#include "log.h"

#include "json_reading.h"
#include "registry.h"
#include "storage_task.h"
#include "supervisor.h"
#include "w25q.h"

#define STORAGE_STACK_WORDS  768U
#define STORAGE_PRIORITY     (tskIDLE_PRIORITY + 1U)
#define STORAGE_QUEUE_DEPTH  8U

static StaticTask_t task_tcb;
static StackType_t  task_stack[STORAGE_STACK_WORDS];
/* queue item: a reading to log, or "run the pending storage_call() job" */
typedef enum { MSG_READING = 0, MSG_CALL } msg_kind_t;
typedef struct {
    uint8_t   kind;
    reading_t r;
} storage_msg_t;

static StaticQueue_t q_ctrl;
static uint8_t       q_storage[STORAGE_QUEUE_DEPTH * sizeof(storage_msg_t)];
static QueueHandle_t q;

/* one storage_call() at a time: call_lock serialises callers, call_done reports the end */
typedef enum { CALL_IDLE = 0, CALL_PENDING, CALL_RUNNING, CALL_DONE, CALL_CANCELLED } call_state_t;
static StaticSemaphore_t call_lock_buf;
static SemaphoreHandle_t call_lock;
static StaticSemaphore_t call_done_buf;
static SemaphoreHandle_t call_done;
static storage_fn_t      call_fn;
static void             *call_arg;
static int32_t           call_result;
static volatile uint8_t  call_state;           /* call_state_t, changed under a critical section */

static FATFS fs;
static FIL   file;
static bool  mounted;
static bool  file_open;
static uint32_t lines;
static uint32_t unsynced;
static uint32_t write_errors;
static sup_id_t sup_id = SUP_INVALID;

/* RAM tail: newest STORAGE_TAIL readings (ring), guarded by a critical section */
static reading_t tail[STORAGE_TAIL];
static uint32_t  tail_count;          /* total pushed; index = count % STORAGE_TAIL */

static void tail_push(const reading_t *r)
{
    taskENTER_CRITICAL();
    tail[tail_count % STORAGE_TAIL] = *r;
    tail_count++;
    taskEXIT_CRITICAL();
}

/* registry "history": {"persisted":true,"lines":123,"records":[{...},...]} oldest first */
static reg_status_t get_history(json_writer_t *w, void *ctx)
{
    (void)ctx;
    reading_t copy[STORAGE_TAIL];
    uint32_t n;

    taskENTER_CRITICAL();
    n = tail_count;
    memcpy(copy, tail, sizeof copy);
    taskEXIT_CRITICAL();

    jw_kv_bool(w, "persisted", mounted);
    jw_kv_uint(w, "lines", lines);
    jw_key(w, "records");
    jw_array_begin(w);
    uint32_t first = (n > STORAGE_TAIL) ? n - STORAGE_TAIL : 0U;
    for (uint32_t i = first; i < n; i++) {
        json_reading_put(w, &copy[i % STORAGE_TAIL]);
    }
    jw_array_end(w);
    return REG_OK;
}

static const reg_entry_t history_entry = {
    "history", "last readings (RAM tail) and log file state", get_history, NULL, NULL
};

static bool open_log(void)
{
    FRESULT fr = f_open(&file, STORAGE_FILE, FA_WRITE | FA_OPEN_APPEND);
    file_open = (fr == FR_OK);
    if (!file_open) {
        LOG_E("open %s: %d", STORAGE_FILE, (int)fr);
    }
    return file_open;
}

static void rotate_if_needed(void)
{
    if (f_size(&file) < STORAGE_MAX_BYTES) {
        return;
    }
    (void)f_close(&file);
    file_open = false;
    (void)f_unlink(STORAGE_FILE_OLD);
    if (f_rename(STORAGE_FILE, STORAGE_FILE_OLD) == FR_OK) {
        LOG_I("rotated %s -> %s", STORAGE_FILE, STORAGE_FILE_OLD);
    }
    (void)open_log();
}

static bool mount(void)
{
    (void)w25q_init();                                 /* probe here, after the scheduler started */
    if (!w25q_present()) {
        return false;
    }
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr == FR_NO_FILESYSTEM) {
        static BYTE work[FF_MAX_SS];                   /* only used once, by f_mkfs */
        const MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        LOG_I("no file system, formatting");
        fr = f_mkfs("", &opt, work, sizeof work);
        if (fr == FR_OK) {
            fr = f_mount(&fs, "", 1);
        }
    }
    if (fr != FR_OK) {
        LOG_D("mount: %d", (int)fr);
        return false;
    }
    return open_log();
}

static void append(const reading_t *r)
{
    char line[JSON_READING_MAX + 1U];
    size_t n = json_reading_write(line, sizeof line - 1U, r);
    if (n == 0U) {
        return;
    }
    line[n++] = '\n';

    UINT bw = 0U;
    if (f_write(&file, line, (UINT)n, &bw) != FR_OK || bw != n) {
        write_errors++;
        LOG_W("write failed (%lu)", (unsigned long)write_errors);
        return;
    }
    lines++;
    if (++unsynced >= STORAGE_SYNC_EVERY) {
        unsynced = 0U;
        (void)f_sync(&file);
        rotate_if_needed();
    }
}

/* MSG_CALL: run the job unless the caller gave up before it started */
static void run_call(void)
{
    taskENTER_CRITICAL();
    bool run = (call_state == CALL_PENDING);
    if (run) {
        call_state = CALL_RUNNING;
    }
    taskEXIT_CRITICAL();
    if (run) {
        call_result = call_fn(call_arg);
        call_state = CALL_DONE;
        (void)xSemaphoreGive(call_done);
    }
}

static void storage_task(void *arg)
{
    (void)arg;
    storage_msg_t m;

    mounted = mount();
    if (mounted) {
        LOG_I("mounted, %s %lu bytes", STORAGE_FILE, (unsigned long)f_size(&file));
    } else {
        LOG_W("no media, logging off");
    }

    for (;;) {
        BaseType_t got = xQueueReceive(q, &m, pdMS_TO_TICKS(1000U));
        sup_checkin(sup_id);
        if (got != pdPASS) {
            continue;
        }
        if (m.kind == MSG_CALL) {
            run_call();
            continue;
        }
        tail_push(&m.r);
        if (mounted && file_open) {
            append(&m.r);
        }
    }
}

void storage_start(void)
{
    q = xQueueCreateStatic(STORAGE_QUEUE_DEPTH, sizeof(storage_msg_t), q_storage, &q_ctrl);
    configASSERT(q != NULL);
    call_lock = xSemaphoreCreateMutexStatic(&call_lock_buf);
    call_done = xSemaphoreCreateBinaryStatic(&call_done_buf);
    configASSERT(call_lock != NULL && call_done != NULL);
    (void)registry_register(&history_entry);
    sup_id = sup_register("storage", 30000U);        /* formatting a blank chip takes a while */
    TaskHandle_t h = xTaskCreateStatic(storage_task, "storage", STORAGE_STACK_WORDS, NULL, STORAGE_PRIORITY,
                                       task_stack, &task_tcb);
    configASSERT(h != NULL);
}

bool storage_submit(const reading_t *r)
{
    if (q == NULL) {
        return false;
    }
    storage_msg_t m = { .kind = MSG_READING, .r = *r };
    return xQueueSend(q, &m, 0) == pdPASS;
}

bool storage_call(storage_fn_t fn, void *arg, int32_t *result, uint32_t timeout_ms)
{
    const TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    bool ok = false;

    if (q == NULL || fn == NULL || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return false;
    }
    if (xSemaphoreTake(call_lock, wait) != pdTRUE) {
        return false;
    }
    call_fn = fn;
    call_arg = arg;
    call_state = CALL_PENDING;
    const storage_msg_t m = { .kind = MSG_CALL };
    if (xQueueSend(q, &m, wait) == pdPASS) {
        if (xSemaphoreTake(call_done, wait) == pdTRUE) {
            ok = true;
        } else {
            /* not finished in time: cancel it if it has not started, else wait for the end
               (a started FatFs call is bounded by the W25Q driver timeouts) */
            taskENTER_CRITICAL();
            bool started = (call_state != CALL_PENDING);
            if (!started) {
                call_state = CALL_CANCELLED;
            }
            taskEXIT_CRITICAL();
            if (started) {
                (void)xSemaphoreTake(call_done, portMAX_DELAY);
                ok = true;
            }
        }
    }
    if (ok && result != NULL) {
        *result = call_result;
    }
    if (call_state != CALL_CANCELLED) {
        call_state = CALL_IDLE;
    }
    (void)xSemaphoreGive(call_lock);
    return ok;
}

bool storage_persistent(void)
{
    return mounted;
}

uint32_t storage_lines_written(void)
{
    return lines;
}
