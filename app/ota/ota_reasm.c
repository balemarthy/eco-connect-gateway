#include <string.h>

#include "ota_reasm.h"

ota_ra_status_t ota_reasm_init(ota_reasm_t *r, const ota_storage_t *st, uint32_t image_size,
                               uint32_t block_size, uint32_t max_size)
{
    memset(r, 0, sizeof *r);
    r->st = st;
    if (block_size == 0U) {
        return OTA_RA_BAD_BLOCK_SIZE;
    }
    if (image_size == 0U) {
        return OTA_RA_EMPTY;
    }
    uint32_t cap = (st != NULL && st->capacity != NULL) ? st->capacity(st->ctx) : 0U;
    uint32_t n = image_size / block_size + ((image_size % block_size) != 0U ? 1U : 0U);
    if (image_size > max_size || image_size > cap || n > OTA_REASM_MAX_BLOCKS) {
        return OTA_RA_TOO_LARGE;
    }
    r->image_size = image_size;
    r->block_size = block_size;
    r->n_blocks = n;
    return OTA_RA_OK;
}

bool ota_reasm_have(const ota_reasm_t *r, uint32_t index)
{
    return index < r->n_blocks && (r->bitmap[index / 8U] & (uint8_t)(1U << (index % 8U))) != 0U;
}

uint32_t ota_reasm_block_len(const ota_reasm_t *r, uint32_t index)
{
    if (index >= r->n_blocks) {
        return 0U;
    }
    if (index + 1U < r->n_blocks) {
        return r->block_size;
    }
    return r->image_size - index * r->block_size;          /* last block: the remainder */
}

ota_blk_status_t ota_reasm_add(ota_reasm_t *r, uint32_t index, const uint8_t *data, size_t len)
{
    if (index >= r->n_blocks) {
        r->rejected++;
        return OTA_BLK_BAD_INDEX;
    }
    if (len != (size_t)ota_reasm_block_len(r, index) || data == NULL) {
        r->rejected++;
        return OTA_BLK_BAD_LENGTH;
    }
    if (ota_reasm_have(r, index)) {
        r->duplicates++;
        return OTA_BLK_DUPLICATE;
    }
    if (r->st->write(r->st->ctx, index * r->block_size, data, len) != OTA_ST_OK) {
        return OTA_BLK_STORAGE;
    }
    r->bitmap[index / 8U] |= (uint8_t)(1U << (index % 8U));
    r->received++;
    return OTA_BLK_NEW;
}

bool ota_reasm_complete(const ota_reasm_t *r)
{
    return r->n_blocks > 0U && r->received == r->n_blocks;
}

int32_t ota_reasm_next_missing(const ota_reasm_t *r)
{
    for (uint32_t i = 0U; i < r->n_blocks; i++) {
        if (!ota_reasm_have(r, i)) {
            return (int32_t)i;
        }
    }
    return -1;
}
