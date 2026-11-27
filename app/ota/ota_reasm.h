/*
 * OTA block reassembly: one bit per block, blocks in any order, duplicates ignored.
 *
 * The image is cut into blocks of block_size bytes; only the last block may be
 * shorter (image_size - (n_blocks - 1) * block_size). A block is accepted only with
 * exactly that length (same rule as the legacy AWS OTA agent's validateDataBlock()),
 * written to the store at index * block_size, and marked in the bitmap. The image is
 * complete when every bit is set. Pure C, no allocation: the bitmap is sized for
 * OTA_REASM_MAX_BLOCKS at compile time (16 blocks = 2 bytes for the 16 KB RAM store).
 */
#ifndef OTA_REASM_H
#define OTA_REASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "ota_storage.h"

#ifndef OTA_REASM_MAX_BLOCKS
#define OTA_REASM_MAX_BLOCKS  ((OTA_IMAGE_LIMIT + OTA_BLOCK_SIZE - 1U) / OTA_BLOCK_SIZE)
#endif

typedef enum {
    OTA_RA_OK = 0,
    OTA_RA_EMPTY,            /* image_size 0 */
    OTA_RA_TOO_LARGE,        /* over max_size, the store capacity or the bitmap */
    OTA_RA_BAD_BLOCK_SIZE    /* block_size 0 */
} ota_ra_status_t;

typedef enum {
    OTA_BLK_NEW = 0,         /* stored and marked */
    OTA_BLK_DUPLICATE,       /* already had it: not written again */
    OTA_BLK_BAD_INDEX,       /* index >= n_blocks */
    OTA_BLK_BAD_LENGTH,      /* not the expected length for this index */
    OTA_BLK_STORAGE          /* the store refused the write */
} ota_blk_status_t;

typedef struct {
    const ota_storage_t *st;
    uint32_t image_size;
    uint32_t block_size;
    uint32_t n_blocks;
    uint32_t received;       /* distinct blocks stored */
    uint32_t duplicates;
    uint32_t rejected;       /* bad index or length */
    uint8_t  bitmap[(OTA_REASM_MAX_BLOCKS + 7U) / 8U];
} ota_reasm_t;

/* Checks the size limits and clears the bitmap. Does not open the store. */
ota_ra_status_t  ota_reasm_init(ota_reasm_t *r, const ota_storage_t *st, uint32_t image_size,
                                uint32_t block_size, uint32_t max_size);
ota_blk_status_t ota_reasm_add(ota_reasm_t *r, uint32_t index, const uint8_t *data, size_t len);

bool     ota_reasm_complete(const ota_reasm_t *r);
bool     ota_reasm_have(const ota_reasm_t *r, uint32_t index);
/* Lowest missing block index, or -1 when complete. */
int32_t  ota_reasm_next_missing(const ota_reasm_t *r);
/* Expected length of block index (0 if out of range). */
uint32_t ota_reasm_block_len(const ota_reasm_t *r, uint32_t index);

#endif /* OTA_REASM_H */
