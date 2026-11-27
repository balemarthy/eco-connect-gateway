/*
 * SPDX-License-Identifier: MIT
 *
 * IO layer internals: the driver interface, the board descriptor and the per-peripheral
 * control block. Only io_core.c, the drivers (io_uart.c, io_spi.c, io_i2c.c), the board
 * file and the host tests include this; applications use io.h.
 *
 *   io_desc_t        const, one per board entry: path, type, driver, hardware config,
 *                    driver state, default modes and timeouts, and the static memory and
 *                    mode objects for the buffered modes (only where the board reserves it)
 *   io_peripheral_t  RAM, one per board entry: open state, current modes, timeouts,
 *                    write mutex, statistics, driver run-time fields
 *   io_driver_t      what a peripheral type must provide (open / write / read / ioctl /
 *                    mode_changed); io_core.c handles everything that is not hardware
 */
#ifndef IO_DRIVER_H
#define IO_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "io_mode.h"

typedef enum {
    IO_TYPE_UART = 0,
    IO_TYPE_SPI,
    IO_TYPE_I2C,
    IO_TYPE_COUNT
} io_type_t;

typedef struct {
    /* hardware init from desc->hw; called once, by the first io_open() */
    io_status_t (*open)(io_peripheral_t *p);
    /* polled / buffered transfers; set p->last, return bytes moved */
    size_t      (*write)(io_peripheral_t *p, const uint8_t *buf, size_t len);
    size_t      (*read)(io_peripheral_t *p, uint8_t *buf, size_t len);
    /* requests io_core.c does not handle itself (speed, priority, SPI_*, I2C_*);
       also sees IO_IOCTL_RELEASE_WRITE_MUTEX first (SPI: drop a forgotten chip select) */
    io_status_t (*ioctl)(io_peripheral_t *p, uint32_t request, void *value);
    /* after a TX (tx = true) or RX mode change: switch interrupts on or off */
    void        (*mode_changed)(io_peripheral_t *p, bool tx);
} io_driver_t;

typedef struct {
    const char        *path;            /* "/UART1/" */
    io_type_t          type;
    const io_driver_t *drv;
    const void        *hw;              /* driver-specific: io_uart_hw_t, io_spi_hw_t, io_i2c_hw_t */
    void              *priv;            /* driver state for this entry (HAL handle) */
    io_mode_t          tx_default;
    io_mode_t          rx_default;
    uint32_t           tx_default_len;  /* queue length for a CHAR_QUEUE default */
    uint32_t           rx_default_len;  /* ring / queue size for a buffered RX default */
    uint32_t           tx_timeout_ms;
    uint32_t           rx_timeout_ms;
    uint8_t           *tx_storage;      /* static memory for char-queue TX, tx_reserved bytes */
    uint32_t           tx_reserved;
    uint8_t           *rx_storage;      /* static memory for circular buffer / char-queue RX */
    uint32_t           rx_reserved;
    io_cq_t           *txq;             /* mode objects, usable only when the reserve is > 0 */
    io_cq_t           *rxq;
    io_cb_t           *rxb;
    uint8_t            cs_count;        /* SPI: chip selects on this bus */
} io_desc_t;

struct io_peripheral {
    const io_desc_t *desc;
    bool             is_open;
    io_mode_t        tx_mode;
    io_mode_t        rx_mode;
    uint32_t         tx_timeout_ms;
    uint32_t         rx_timeout_ms;
    io_status_t      last;              /* of the last io_read / io_write */
    io_stats_t       stats;
    io_zc_t          zc;                /* write mutex (zero-copy TX) */
    io_cq_t         *txq;               /* char-queue TX (copied from the descriptor) */
    io_cq_t         *rxq;               /* char-queue RX */
    io_cb_t         *rxb;               /* circular-buffer RX */
    /* driver run-time fields */
    int8_t           cs;                /* SPI: selected chip select, -1 none */
    uint8_t          i2c_addr;          /* I2C: 7-bit slave address */
    const uint8_t *volatile zc_next;    /* UART zero copy: next byte to send */
    volatile size_t  zc_left;
    void            *priv;              /* driver state (desc->priv) */
};

typedef struct {
    const char      *name;
    const io_desc_t *devices;
    io_peripheral_t *ctl;               /* control blocks, same order as devices */
    size_t           count;
} io_board_t;

/* the board in this build (io_board.c on the target, tests/io_board_host.c on the host) */
extern const io_board_t io_board;

/* ---- pure helpers in io_core.c (host-tested) ---------------------------------------- */
/* Index of path in the board table, -1 if absent. The path must be "/NAME/" exactly. */
int         io_find(const io_board_t *b, const char *path);
bool        io_path_valid(const char *path);
/* Can a peripheral of this descriptor take this request with this value? */
io_status_t io_ioctl_check(const io_desc_t *d, uint32_t request, const void *value);
/* Is mode m allowed for direction tx on this peripheral type? */
bool        io_mode_allowed(io_type_t type, bool tx, io_mode_t m);

#endif /* IO_DRIVER_H */
