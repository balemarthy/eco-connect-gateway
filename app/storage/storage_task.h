/*
 * Storage task: appends every reading as one JSON line to READINGS.LOG on the
 * W25Q (FatFs) and keeps the last STORAGE_TAIL readings in RAM for GET /log.
 *
 * No media (no chip, or the W25Q data path not written yet): the task logs
 * "no media, logging off" once and keeps serving the RAM tail.
 * Only this task touches FatFs, so FatFs runs without re-entrancy locks.
 */
#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "readings.h"

#define STORAGE_FILE        "READINGS.LOG"
#define STORAGE_FILE_OLD    "READINGS.OLD"
#define STORAGE_MAX_BYTES   (256UL * 1024UL)    /* rotate to .OLD beyond this */
#define STORAGE_SYNC_EVERY  16U                 /* f_sync after this many lines (flash wear) */
#define STORAGE_TAIL        8U

/* Creates the queue and the task, registers the "history" registry key. */
void storage_start(void);

/* Non-blocking hand-over from the app task; false if the queue is full. */
bool storage_submit(const reading_t *r);

bool     storage_persistent(void);   /* file system mounted */
uint32_t storage_lines_written(void);

#endif /* STORAGE_TASK_H */
