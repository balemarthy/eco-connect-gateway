/*
 * OTA image store interface (Virtual API: a table of function pointers + context).
 *
 * The state machine only talks to this interface, so the image can land in a RAM
 * buffer (host tests, and the target without flash work), a file on the FatFs
 * volume, or later an internal-flash slot, without touching the download logic.
 * This job adds no flash bank swap and no bootloader: READY means "a verified
 * image is stored", not "installed".
 *
 *   open(size)            start a new image of exactly size bytes (truncate / erase)
 *   write(off, data, n)   blocks may arrive in any order; off + n <= size
 *   read(off, data, n)    used by verification (CRC over the stored bytes)
 *   close()               all blocks written: flush, keep the image
 *   abort()               drop a partial image
 *   capacity()            largest image this store can hold
 */
#ifndef OTA_STORAGE_H
#define OTA_STORAGE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    OTA_ST_OK = 0,
    OTA_ST_ERR_SIZE,        /* larger than capacity, or write/read outside the image */
    OTA_ST_ERR_STATE,       /* not open */
    OTA_ST_ERR_IO           /* backend error (file system, flash) */
} ota_st_status_t;

typedef struct {
    const char *name;
    ota_st_status_t (*open)(void *ctx, uint32_t size);
    ota_st_status_t (*write)(void *ctx, uint32_t offset, const uint8_t *data, size_t len);
    ota_st_status_t (*read)(void *ctx, uint32_t offset, uint8_t *data, size_t len);
    ota_st_status_t (*close)(void *ctx);
    ota_st_status_t (*abort)(void *ctx);
    uint32_t        (*capacity)(void *ctx);
    void *ctx;
} ota_storage_t;

#endif /* OTA_STORAGE_H */
