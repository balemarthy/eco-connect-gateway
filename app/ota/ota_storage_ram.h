/*
 * OTA image store in a caller-provided RAM buffer. Pure C (host tests and target).
 * On the target the buffer is a static array of OTA_MAX_IMAGE_SIZE bytes (ota_task.c).
 */
#ifndef OTA_STORAGE_RAM_H
#define OTA_STORAGE_RAM_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_storage.h"

typedef struct {
    uint8_t  *buf;
    uint32_t  cap;
    uint32_t  size;         /* size of the image being received / stored */
    bool      is_open;
    bool      complete;     /* close() called: image kept */
} ota_ram_store_t;

/* Fills *st with the RAM backend working on buf[0..cap). */
void ota_storage_ram_init(ota_storage_t *st, ota_ram_store_t *ram, uint8_t *buf, uint32_t cap);

#endif /* OTA_STORAGE_RAM_H */
