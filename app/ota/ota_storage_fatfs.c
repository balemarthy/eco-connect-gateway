#include "app_config.h"
#include "ota_storage_fatfs.h"

static ota_st_status_t fat_open(void *ctx, uint32_t size)
{
    ota_fatfs_store_t *s = ctx;
    if (s->is_open || s->complete) {
        (void)f_close(&s->file);
    }
    s->is_open = false;
    s->complete = false;
    if (size == 0U || size > OTA_MAX_IMAGE_SIZE_FATFS) {
        return OTA_ST_ERR_SIZE;
    }
    if (f_open(&s->file, OTA_FATFS_FILE, FA_CREATE_ALWAYS | FA_WRITE | FA_READ) != FR_OK) {
        return OTA_ST_ERR_IO;
    }
    /* reserve the whole size up front, so out-of-order blocks seek inside the file */
    if (f_lseek(&s->file, size) != FR_OK || f_tell(&s->file) != size || f_lseek(&s->file, 0) != FR_OK) {
        (void)f_close(&s->file);
        return OTA_ST_ERR_IO;
    }
    s->size = size;
    s->is_open = true;
    return OTA_ST_OK;
}

static bool in_range(const ota_fatfs_store_t *s, uint32_t off, size_t len)
{
    return off <= s->size && len <= (size_t)(s->size - off);
}

static ota_st_status_t fat_write(void *ctx, uint32_t off, const uint8_t *data, size_t len)
{
    ota_fatfs_store_t *s = ctx;
    UINT bw = 0U;
    if (!s->is_open) {
        return OTA_ST_ERR_STATE;
    }
    if (!in_range(s, off, len)) {
        return OTA_ST_ERR_SIZE;
    }
    if (f_lseek(&s->file, off) != FR_OK || f_write(&s->file, data, (UINT)len, &bw) != FR_OK ||
        bw != (UINT)len) {
        return OTA_ST_ERR_IO;
    }
    return OTA_ST_OK;
}

static ota_st_status_t fat_read(void *ctx, uint32_t off, uint8_t *data, size_t len)
{
    ota_fatfs_store_t *s = ctx;
    UINT br = 0U;
    if (!s->is_open && !s->complete) {
        return OTA_ST_ERR_STATE;
    }
    if (!in_range(s, off, len)) {
        return OTA_ST_ERR_SIZE;
    }
    if (f_lseek(&s->file, off) != FR_OK || f_read(&s->file, data, (UINT)len, &br) != FR_OK ||
        br != (UINT)len) {
        return OTA_ST_ERR_IO;
    }
    return OTA_ST_OK;
}

static ota_st_status_t fat_close(void *ctx)
{
    ota_fatfs_store_t *s = ctx;
    if (!s->is_open) {
        return OTA_ST_ERR_STATE;
    }
    if (f_sync(&s->file) != FR_OK) {
        return OTA_ST_ERR_IO;
    }
    s->is_open = false;
    s->complete = true;                              /* file stays open for verification reads */
    return OTA_ST_OK;
}

static ota_st_status_t fat_abort(void *ctx)
{
    ota_fatfs_store_t *s = ctx;
    if (s->is_open || s->complete) {
        (void)f_close(&s->file);
        (void)f_unlink(OTA_FATFS_FILE);
    }
    s->is_open = false;
    s->complete = false;
    s->size = 0U;
    return OTA_ST_OK;
}

static uint32_t fat_capacity(void *ctx)
{
    (void)ctx;
    return OTA_MAX_IMAGE_SIZE_FATFS;
}

void ota_storage_fatfs_init(ota_storage_t *st, ota_fatfs_store_t *fs)
{
    fs->size = 0U;
    fs->is_open = false;
    fs->complete = false;

    st->name = "fatfs";
    st->open = fat_open;
    st->write = fat_write;
    st->read = fat_read;
    st->close = fat_close;
    st->abort = fat_abort;
    st->capacity = fat_capacity;
    st->ctx = fs;
}
