/*
 * OTA image store as a file (OTA_FATFS_FILE) on the FatFs volume (W25Q).
 * Selected with OTA_STORAGE = OTA_STORAGE_FATFS in app_config.h (default: RAM).
 *
 * LIMITS - read before selecting it:
 *   - FatFs is built with FF_FS_REENTRANT 0 because only the storage task touches it
 *     (storage_task.h). Using this backend from the OTA task breaks that rule; enable
 *     FF_FS_REENTRANT (ffsystem.c mutexes) or route the calls through the storage task
 *     before using it on hardware.
 *   - The volume must be mounted (the storage task mounts it); the W25Q read/program
 *     paths are mentee tasks, so today open() fails with OTA_ST_ERR_IO.
 * Compiled in the ota build so it stays warning-free; linked only when selected.
 */
#ifndef OTA_STORAGE_FATFS_H
#define OTA_STORAGE_FATFS_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"
#include "ota_storage.h"

typedef struct {
    FIL      file;
    uint32_t size;
    bool     is_open;
    bool     complete;
} ota_fatfs_store_t;

void ota_storage_fatfs_init(ota_storage_t *st, ota_fatfs_store_t *fs);

#endif /* OTA_STORAGE_FATFS_H */
