/*
 * OTA image store as a file (OTA_FATFS_FILE) on the FatFs volume (W25Q).
 * Default store of make DEMO=ota (OTA_STORAGE = OTA_STORAGE_FATFS); make DEMO=ota
 * OTA_STORE=ram selects the RAM store instead.
 *
 * FatFs is built with FF_FS_REENTRANT 0 because only the storage task touches it
 * (storage_task.h). This store therefore never calls FatFs itself: every operation is a
 * job for storage_call(), which runs it in the storage task. The request (offset, pointers,
 * length) lives in the store object, not on the caller's stack, and storage_call() cancels
 * a job that has not started when the caller stops waiting.
 *
 * The volume must be mounted (the storage task mounts it at start). The W25Q read/program
 * paths are mentee tasks, so until they exist open() fails with OTA_ST_ERR_IO and the
 * download ends in FAILED / "storage error".
 */
#ifndef OTA_STORAGE_FATFS_H
#define OTA_STORAGE_FATFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ff.h"
#include "ota_storage.h"

/* longest a single store operation may wait for the storage task to pick it up */
#ifndef OTA_FATFS_CALL_TIMEOUT_MS
#define OTA_FATFS_CALL_TIMEOUT_MS  5000U
#endif

typedef struct {
    FIL      file;
    uint32_t size;
    bool     is_open;
    bool     complete;
    /* request for the job running in the storage task */
    uint32_t       req_off;
    const uint8_t *req_wr;
    uint8_t       *req_rd;
    size_t         req_len;
} ota_fatfs_store_t;

void ota_storage_fatfs_init(ota_storage_t *st, ota_fatfs_store_t *fs);

#endif /* OTA_STORAGE_FATFS_H */
