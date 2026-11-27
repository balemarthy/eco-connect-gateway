/*
 * SPDX-License-Identifier: MIT
 *
 * IO layer core: open by path, read/write/ioctl dispatch, request validation and the
 * transfer-mode rules. Hardware is reached only through the io_driver_t of each board
 * entry, so this file builds unchanged on the host against a test board table.
 */
#include <string.h>

#include "io_driver.h"

#define IO_PATH_MAX  16U                 /* "/" + name + "/" */

static const char *const status_names[IO_STATUS_COUNT] = {
    "ok", "bad parameter", "unsupported", "no memory", "wrong state", "not owner",
    "timeout", "io error", "not ready"
};

const char *io_status_str(io_status_t st)
{
    return ((unsigned)st < IO_STATUS_COUNT) ? status_names[st] : "?";
}

const char *io_path(io_handle_t h)
{
    return (h != NULL && h->desc != NULL) ? h->desc->path : "?";
}

/* ---------------------------------------------------------------- lookup */

bool io_path_valid(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return false;
    }
    size_t n = strlen(path);
    if (n < 3U || n > IO_PATH_MAX || path[n - 1U] != '/') {
        return false;
    }
    for (size_t i = 1U; i + 1U < n; i++) {
        char c = path[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

int io_find(const io_board_t *b, const char *path)
{
    if (b == NULL || !io_path_valid(path)) {
        return -1;
    }
    for (size_t i = 0U; i < b->count; i++) {
        if (strcmp(b->devices[i].path, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------- rules */

bool io_mode_allowed(io_type_t type, bool tx, io_mode_t m)
{
    switch (m) {
    case IO_MODE_POLLED:
        return type < IO_TYPE_COUNT;
    case IO_MODE_ZERO_COPY:
        return tx && type < IO_TYPE_COUNT;              /* every type: TX side only */
    case IO_MODE_CHAR_QUEUE:
        return type == IO_TYPE_UART;
    case IO_MODE_CIRC_BUF:
        return type == IO_TYPE_UART && !tx;
    default:
        return false;
    }
}

static io_status_t check_range(uint32_t v, uint32_t lo, uint32_t hi)
{
    return (v >= lo && v <= hi) ? IO_OK : IO_ERR_PARAM;
}

io_status_t io_ioctl_check(const io_desc_t *d, uint32_t request, const void *value)
{
    if (d == NULL) {
        return IO_ERR_PARAM;
    }
    const uint32_t v = (uint32_t)(uintptr_t)value;
    const io_type_t t = d->type;

    switch (request) {
    case IO_IOCTL_USE_POLLED_TX:
    case IO_IOCTL_USE_POLLED_RX:
    case IO_IOCTL_USE_ZERO_COPY_TX:
    case IO_IOCTL_OBTAIN_WRITE_MUTEX:
    case IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE:
    case IO_IOCTL_RELEASE_WRITE_MUTEX:
    case IO_IOCTL_SET_TX_TIMEOUT:
    case IO_IOCTL_SET_RX_TIMEOUT:
        return IO_OK;

    case IO_IOCTL_USE_CHAR_QUEUE_TX:
    case IO_IOCTL_USE_CHAR_QUEUE_RX: {
        bool tx = (request == IO_IOCTL_USE_CHAR_QUEUE_TX);
        if (!io_mode_allowed(t, tx, IO_MODE_CHAR_QUEUE)) {
            return IO_ERR_UNSUPPORTED;
        }
        if (v == 0U) {
            return IO_ERR_PARAM;
        }
        return (v <= (tx ? d->tx_reserved : d->rx_reserved)) ? IO_OK : IO_ERR_NO_MEMORY;
    }
    case IO_IOCTL_USE_CIRCULAR_BUFFER_RX:
        if (!io_mode_allowed(t, false, IO_MODE_CIRC_BUF)) {
            return IO_ERR_UNSUPPORTED;
        }
        if (!io_cb_size_ok(v, v)) {
            return IO_ERR_PARAM;                         /* power of two, >= IO_CB_MIN_SIZE */
        }
        return (v <= d->rx_reserved) ? IO_OK : IO_ERR_NO_MEMORY;

    case IO_IOCTL_CLEAR_RX_BUFFER:
    case IO_IOCTL_WAIT_TX_EMPTY:
        return (t == IO_TYPE_UART) ? IO_OK : IO_ERR_UNSUPPORTED;

    case IO_IOCTL_SET_SPEED:
        switch (t) {
        case IO_TYPE_UART: return check_range(v, IO_UART_BAUD_MIN, IO_UART_BAUD_MAX);
        case IO_TYPE_SPI:  return check_range(v, IO_SPI_HZ_MIN, IO_SPI_HZ_MAX);
        case IO_TYPE_I2C:  return check_range(v, IO_I2C_HZ_MIN, IO_I2C_HZ_MAX);
        default:           return IO_ERR_UNSUPPORTED;
        }
    case IO_IOCTL_SET_INTERRUPT_PRIORITY:
        if (t != IO_TYPE_UART) {
            return IO_ERR_UNSUPPORTED;                   /* SPI and I2C run polled: no IRQ */
        }
        return check_range(v, IO_IRQ_PRIO_MIN, IO_IRQ_PRIO_MAX);

    case IO_IOCTL_SPI_SET_MODE:
        if (t != IO_TYPE_SPI) {
            return IO_ERR_UNSUPPORTED;
        }
        return (v <= 3U) ? IO_OK : IO_ERR_PARAM;
    case IO_IOCTL_SPI_SELECT:
        if (t != IO_TYPE_SPI) {
            return IO_ERR_UNSUPPORTED;
        }
        return (v < d->cs_count) ? IO_OK : IO_ERR_PARAM;
    case IO_IOCTL_SPI_DESELECT:
        return (t == IO_TYPE_SPI) ? IO_OK : IO_ERR_UNSUPPORTED;
    case IO_IOCTL_SPI_TRANSFER:
        if (t != IO_TYPE_SPI) {
            return IO_ERR_UNSUPPORTED;
        }
        return (value != NULL) ? IO_OK : IO_ERR_PARAM;

    case IO_IOCTL_I2C_SET_SLAVE_ADDRESS:
        if (t != IO_TYPE_I2C) {
            return IO_ERR_UNSUPPORTED;
        }
        return check_range(v, IO_I2C_ADDR_MIN, IO_I2C_ADDR_MAX);
    case IO_IOCTL_I2C_WRITE_READ: {
        if (t != IO_TYPE_I2C) {
            return IO_ERR_UNSUPPORTED;
        }
        const io_i2c_xfer_t *x = value;
        if (x == NULL || x->wr == NULL || x->rd == NULL || (x->wr_len != 1U && x->wr_len != 2U) ||
            x->rd_len == 0U || x->rd_len > 0xFFFFU) {
            return IO_ERR_PARAM;
        }
        return IO_OK;
    }

    case IO_IOCTL_GET_LAST_STATUS:
    case IO_IOCTL_GET_STATS:
        return (value != NULL) ? IO_OK : IO_ERR_PARAM;

    default:
        return IO_ERR_UNSUPPORTED;
    }
}

/* ---------------------------------------------------------------- modes */

static io_status_t set_mode(io_peripheral_t *p, bool tx, io_mode_t m, uint32_t len)
{
    const io_desc_t *d = p->desc;
    io_status_t st = IO_OK;

    if (!io_mode_allowed(d->type, tx, m)) {
        return IO_ERR_UNSUPPORTED;
    }
    if (tx && io_zc_held(&p->zc)) {
        return IO_ERR_STATE;                             /* a zero-copy write owns the TX side */
    }
    /* interrupts off for this direction while its buffer is rebuilt */
    if (tx) {
        p->tx_mode = IO_MODE_POLLED;
    } else {
        p->rx_mode = IO_MODE_POLLED;
    }
    if (d->drv->mode_changed != NULL) {
        d->drv->mode_changed(p, tx);
    }
    switch (m) {
    case IO_MODE_ZERO_COPY:
        st = io_zc_init(&p->zc, d->type == IO_TYPE_UART);
        break;
    case IO_MODE_CHAR_QUEUE:
        if (len == 0U || len > (tx ? d->tx_reserved : d->rx_reserved)) {
            st = (len == 0U) ? IO_ERR_PARAM : IO_ERR_NO_MEMORY;
        } else {
            st = io_cq_init(tx ? p->txq : p->rxq, tx ? d->tx_storage : d->rx_storage, len);
        }
        break;
    case IO_MODE_CIRC_BUF:
        if (!io_cb_size_ok(len, d->rx_reserved)) {
            st = (len > d->rx_reserved) ? IO_ERR_NO_MEMORY : IO_ERR_PARAM;
        } else {
            st = io_cb_init(p->rxb, d->rx_storage, len);
        }
        break;
    default:
        break;
    }
    if (st != IO_OK) {
        return st;                                       /* stays polled */
    }
    if (tx) {
        p->tx_mode = m;
    } else {
        p->rx_mode = m;
    }
    if (d->drv->mode_changed != NULL) {
        d->drv->mode_changed(p, tx);
    }
    return IO_OK;
}

/* ---------------------------------------------------------------- API */

io_handle_t io_open(const char *path, uint32_t flags)
{
    if (flags != 0U) {
        return NULL;
    }
    int i = io_find(&io_board, path);
    if (i < 0) {
        return NULL;
    }
    io_peripheral_t *p = &io_board.ctl[i];
    const io_desc_t *d = &io_board.devices[i];
    if (p->is_open) {
        return p;                                        /* one control block per peripheral */
    }
    if (d->drv == NULL || d->drv->open == NULL || d->drv->write == NULL || d->drv->read == NULL) {
        return NULL;
    }

    const bool running = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    if (running) {
        vTaskSuspendAll();                               /* two first opens must not race */
    }
    io_handle_t h = NULL;
    if (p->is_open) {
        h = p;
    } else {
        p->desc = d;
        p->tx_mode = IO_MODE_POLLED;
        p->rx_mode = IO_MODE_POLLED;
        p->tx_timeout_ms = d->tx_timeout_ms;
        p->rx_timeout_ms = d->rx_timeout_ms;
        p->last = IO_OK;
        memset(&p->stats, 0, sizeof p->stats);
        p->cs = -1;
        p->i2c_addr = 0U;
        p->zc_next = NULL;
        p->zc_left = 0U;
        p->txq = d->txq;
        p->rxq = d->rxq;
        p->rxb = d->rxb;
        p->priv = d->priv;
        if (d->drv->open(p) == IO_OK &&
            (d->tx_default == IO_MODE_POLLED || set_mode(p, true, d->tx_default, d->tx_default_len) == IO_OK) &&
            (d->rx_default == IO_MODE_POLLED || set_mode(p, false, d->rx_default, d->rx_default_len) == IO_OK)) {
            p->is_open = true;
            h = p;
        }
    }
    if (running) {
        (void)xTaskResumeAll();
    }
    return h;
}

size_t io_write(io_handle_t h, const void *buf, size_t len)
{
    if (h == NULL) {
        return 0U;
    }
    if (!h->is_open) {
        h->last = IO_ERR_NOT_READY;
        return 0U;
    }
    if (buf == NULL && len > 0U) {
        h->last = IO_ERR_PARAM;
        return 0U;
    }
    h->last = IO_OK;
    if (len == 0U) {
        return 0U;
    }
    if (h->tx_mode == IO_MODE_ZERO_COPY && !io_zc_caller_holds(&h->zc)) {
        h->last = IO_ERR_NOT_OWNER;
        return 0U;
    }
    size_t n = h->desc->drv->write(h, buf, len);
    h->stats.tx_bytes += (uint32_t)n;
    if (h->last == IO_ERR_TIMEOUT) {
        h->stats.timeouts++;
    }
    return n;
}

size_t io_read(io_handle_t h, void *buf, size_t len)
{
    if (h == NULL) {
        return 0U;
    }
    if (!h->is_open) {
        h->last = IO_ERR_NOT_READY;
        return 0U;
    }
    if (buf == NULL && len > 0U) {
        h->last = IO_ERR_PARAM;
        return 0U;
    }
    h->last = IO_OK;
    if (len == 0U) {
        return 0U;
    }
    size_t n = h->desc->drv->read(h, buf, len);
    h->stats.rx_bytes += (uint32_t)n;
    if (h->last == IO_ERR_TIMEOUT) {
        h->stats.timeouts++;
    }
    return n;
}

io_status_t io_ioctl(io_handle_t h, uint32_t request, void *value)
{
    if (h == NULL) {
        return IO_ERR_PARAM;
    }
    if (!h->is_open) {
        return IO_ERR_NOT_READY;
    }
    io_status_t st = io_ioctl_check(h->desc, request, value);
    if (st != IO_OK) {
        return st;
    }
    const uint32_t v = (uint32_t)(uintptr_t)value;
    const io_driver_t *drv = h->desc->drv;

    switch (request) {
    case IO_IOCTL_USE_POLLED_TX:
        return set_mode(h, true, IO_MODE_POLLED, 0U);
    case IO_IOCTL_USE_POLLED_RX:
        return set_mode(h, false, IO_MODE_POLLED, 0U);
    case IO_IOCTL_USE_ZERO_COPY_TX:
        return set_mode(h, true, IO_MODE_ZERO_COPY, 0U);
    case IO_IOCTL_USE_CHAR_QUEUE_TX:
        return set_mode(h, true, IO_MODE_CHAR_QUEUE, v);
    case IO_IOCTL_USE_CHAR_QUEUE_RX:
        return set_mode(h, false, IO_MODE_CHAR_QUEUE, v);
    case IO_IOCTL_USE_CIRCULAR_BUFFER_RX:
        return set_mode(h, false, IO_MODE_CIRC_BUF, v);

    case IO_IOCTL_OBTAIN_WRITE_MUTEX:
        return (h->tx_mode == IO_MODE_ZERO_COPY) ? io_zc_obtain(&h->zc, v) : IO_ERR_STATE;
    case IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE:
        return (h->tx_mode == IO_MODE_ZERO_COPY) ? io_zc_wait_complete(&h->zc, v) : IO_OK;
    case IO_IOCTL_RELEASE_WRITE_MUTEX:
        if (h->tx_mode != IO_MODE_ZERO_COPY) {
            return IO_ERR_STATE;
        }
        if (!io_zc_caller_holds(&h->zc)) {
            return IO_ERR_NOT_OWNER;
        }
        if (drv->ioctl != NULL) {
            (void)drv->ioctl(h, request, value);         /* SPI: drop a chip select left on */
        }
        return io_zc_release(&h->zc);

    case IO_IOCTL_SET_TX_TIMEOUT:
        h->tx_timeout_ms = v;
        return IO_OK;
    case IO_IOCTL_SET_RX_TIMEOUT:
        h->rx_timeout_ms = v;
        return IO_OK;
    case IO_IOCTL_CLEAR_RX_BUFFER:
        if (h->rx_mode == IO_MODE_CIRC_BUF) {
            io_cb_clear(h->rxb);
        } else if (h->rx_mode == IO_MODE_CHAR_QUEUE) {
            io_cq_clear(h->rxq);
        }
        return IO_OK;
    case IO_IOCTL_WAIT_TX_EMPTY:
        if (h->tx_mode == IO_MODE_CHAR_QUEUE) {
            return io_cq_wait_empty(h->txq, v);
        }
        return (h->tx_mode == IO_MODE_ZERO_COPY) ? io_zc_wait_complete(&h->zc, v) : IO_OK;

    case IO_IOCTL_GET_LAST_STATUS:
        *(io_status_t *)value = h->last;
        return IO_OK;
    case IO_IOCTL_GET_STATS:
        taskENTER_CRITICAL();
        *(io_stats_t *)value = h->stats;
        taskEXIT_CRITICAL();
        return IO_OK;

    default:
        return (drv->ioctl != NULL) ? drv->ioctl(h, request, value) : IO_ERR_UNSUPPORTED;
    }
}
