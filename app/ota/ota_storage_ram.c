#include <string.h>

#include "ota_storage_ram.h"

static ota_st_status_t ram_open(void *ctx, uint32_t size)
{
    ota_ram_store_t *r = ctx;
    if (size == 0U || size > r->cap) {
        return OTA_ST_ERR_SIZE;
    }
    memset(r->buf, 0xFF, size);                 /* like erased flash: unwritten bytes read 0xFF */
    r->size = size;
    r->is_open = true;
    r->complete = false;
    return OTA_ST_OK;
}

static bool in_range(const ota_ram_store_t *r, uint32_t off, size_t len)
{
    return off <= r->size && len <= (size_t)(r->size - off);
}

static ota_st_status_t ram_write(void *ctx, uint32_t off, const uint8_t *data, size_t len)
{
    ota_ram_store_t *r = ctx;
    if (!r->is_open) {
        return OTA_ST_ERR_STATE;
    }
    if (!in_range(r, off, len)) {
        return OTA_ST_ERR_SIZE;
    }
    memcpy(&r->buf[off], data, len);
    return OTA_ST_OK;
}

static ota_st_status_t ram_read(void *ctx, uint32_t off, uint8_t *data, size_t len)
{
    ota_ram_store_t *r = ctx;
    if (!r->is_open && !r->complete) {
        return OTA_ST_ERR_STATE;
    }
    if (!in_range(r, off, len)) {
        return OTA_ST_ERR_SIZE;
    }
    memcpy(data, &r->buf[off], len);
    return OTA_ST_OK;
}

static ota_st_status_t ram_close(void *ctx)
{
    ota_ram_store_t *r = ctx;
    if (!r->is_open) {
        return OTA_ST_ERR_STATE;
    }
    r->is_open = false;
    r->complete = true;
    return OTA_ST_OK;
}

static ota_st_status_t ram_abort(void *ctx)
{
    ota_ram_store_t *r = ctx;
    r->is_open = false;
    r->complete = false;
    r->size = 0U;
    return OTA_ST_OK;
}

static uint32_t ram_capacity(void *ctx)
{
    const ota_ram_store_t *r = ctx;
    return r->cap;
}

void ota_storage_ram_init(ota_storage_t *st, ota_ram_store_t *ram, uint8_t *buf, uint32_t cap)
{
    ram->buf = buf;
    ram->cap = cap;
    ram->size = 0U;
    ram->is_open = false;
    ram->complete = false;

    st->name = "ram";
    st->open = ram_open;
    st->write = ram_write;
    st->read = ram_read;
    st->close = ram_close;
    st->abort = ram_abort;
    st->capacity = ram_capacity;
    st->ctx = ram;
}
