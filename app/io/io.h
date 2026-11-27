/*
 * SPDX-License-Identifier: MIT
 *
 * Peripheral IO layer, modelled on the FreeRTOS+IO API style.
 *
 * Every peripheral the board offers has a path in the board descriptor table
 * (io_board_f411.h). A task opens it by name and then only uses four calls:
 *
 *     io_handle_t u = io_open("/UART1/", 0U);
 *     io_ioctl(u, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, (void *)(uintptr_t)256U);
 *     io_ioctl(u, IO_IOCTL_SET_RX_TIMEOUT, (void *)(uintptr_t)1000U);
 *     size_t n = io_read(u, buf, sizeof buf);        // waits up to 1000 ms for data
 *     io_write(u, frame, len);                       // polled TX with the TX timeout
 *
 * Transfer modes (selected per direction with an ioctl; the board table gives the
 * default and reserves the static memory for the interrupt-driven ones):
 *   polled          the calling task drives the registers; bounded by the TX/RX timeout
 *   circular buffer RX: the RX interrupt fills a static ring, io_read() takes from it
 *   character queue TX and/or RX through a static FreeRTOS queue, the interrupt on the
 *                   other end (TX: io_write() copies and returns once queued)
 *   zero copy       TX straight from the caller's buffer under a write mutex. UART: the
 *                   TX interrupt sends the buffer and releases the mutex at the end.
 *                   SPI / I2C: the mutex is the bus lock; the caller keeps it across as
 *                   many transfers (and chip selects) as it needs and releases it.
 *
 * Rules: no heap (every control block, buffer and kernel object is static), every
 * blocking call has a timeout, io_read()/io_write() return the number of bytes moved
 * and the reason for a short count is in IO_IOCTL_GET_LAST_STATUS. A second io_open()
 * of the same path returns the same handle: there is one control block per peripheral,
 * shared by every task that opens it (that is what the write mutex is for).
 * Integer ioctl values are passed as (void *)(uintptr_t)value, pointers as they are.
 * Not callable from an ISR.
 */
#ifndef IO_H
#define IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    IO_OK = 0,
    IO_ERR_PARAM,           /* NULL handle/pointer, value out of range */
    IO_ERR_UNSUPPORTED,     /* request or mode not offered by this peripheral type */
    IO_ERR_NO_MEMORY,       /* mode needs more static memory than the board reserved */
    IO_ERR_STATE,           /* not now: e.g. mode change while the write mutex is held,
                               SPI transfer without a chip select */
    IO_ERR_NOT_OWNER,       /* zero-copy mode: the caller does not hold the write mutex */
    IO_ERR_TIMEOUT,
    IO_ERR_IO,              /* the peripheral reported an error (NACK, bus error, ...) */
    IO_ERR_NOT_READY,       /* not opened / hardware init failed */
    IO_STATUS_COUNT
} io_status_t;

/* Opaque handle (one static control block per peripheral). NULL = open failed. */
typedef struct io_peripheral io_peripheral_t;
typedef io_peripheral_t *io_handle_t;

/* ioctl requests. Value column: what to pass in the void * argument. */
typedef enum {
    /* transfer modes ------------------------------------------------------------- */
    IO_IOCTL_USE_POLLED_TX = 1,             /* -                                        */
    IO_IOCTL_USE_POLLED_RX,                 /* -                                        */
    IO_IOCTL_USE_ZERO_COPY_TX,              /* -                                        */
    IO_IOCTL_USE_CHAR_QUEUE_TX,             /* queue length (chars)                     */
    IO_IOCTL_USE_CHAR_QUEUE_RX,             /* queue length (chars)                     */
    IO_IOCTL_USE_CIRCULAR_BUFFER_RX,        /* buffer size (bytes)                      */
    /* zero-copy write mutex -------------------------------------------------------- */
    IO_IOCTL_OBTAIN_WRITE_MUTEX = 20,       /* timeout ms                               */
    IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE,  /* timeout ms (take + give back)            */
    IO_IOCTL_RELEASE_WRITE_MUTEX,           /* -                                        */
    /* timeouts and buffers --------------------------------------------------------- */
    IO_IOCTL_SET_TX_TIMEOUT = 30,           /* ms                                       */
    IO_IOCTL_SET_RX_TIMEOUT,                /* ms                                       */
    IO_IOCTL_CLEAR_RX_BUFFER,               /* - (circular buffer / char queue RX)      */
    IO_IOCTL_WAIT_TX_EMPTY,                 /* timeout ms (char queue TX drained)       */
    /* peripheral settings ---------------------------------------------------------- */
    IO_IOCTL_SET_SPEED = 40,                /* UART baud, SPI clock Hz, I2C clock Hz    */
    IO_IOCTL_SET_INTERRUPT_PRIORITY,        /* NVIC priority, IO_IRQ_PRIO_MIN..MAX      */
    /* SPI --------------------------------------------------------------------------- */
    IO_IOCTL_SPI_SET_MODE = 50,             /* 0..3 (CPOL << 1 | CPHA)                  */
    IO_IOCTL_SPI_SELECT,                    /* chip-select index from the board table   */
    IO_IOCTL_SPI_DESELECT,                  /* - (waits for the last bit first)         */
    IO_IOCTL_SPI_TRANSFER,                  /* io_spi_xfer_t * (full duplex)            */
    /* I2C --------------------------------------------------------------------------- */
    IO_IOCTL_I2C_SET_SLAVE_ADDRESS = 60,    /* 7-bit address 0x08..0x77                 */
    IO_IOCTL_I2C_WRITE_READ,                /* io_i2c_xfer_t * (register read)          */
    /* information ------------------------------------------------------------------- */
    IO_IOCTL_GET_LAST_STATUS = 70,          /* io_status_t *                            */
    IO_IOCTL_GET_STATS                      /* io_stats_t *                             */
} io_ioctl_t;

/* limits checked by io_ioctl() */
#define IO_IRQ_PRIO_MIN        5U           /* = configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */
#define IO_IRQ_PRIO_MAX        15U
#define IO_UART_BAUD_MIN       1200U
#define IO_UART_BAUD_MAX       921600U
#define IO_SPI_HZ_MIN          100000U
#define IO_SPI_HZ_MAX          50000000U
#define IO_I2C_HZ_MIN          10000U
#define IO_I2C_HZ_MAX          400000U
#define IO_I2C_ADDR_MIN        0x08U        /* 0x00-0x07 and 0x78-0x7F are reserved */
#define IO_I2C_ADDR_MAX        0x77U

/* IO_IOCTL_SPI_TRANSFER: len bytes out and in at the same time inside the current
   chip select. tx == NULL sends 0xFF, rx == NULL discards what comes back. */
typedef struct {
    const uint8_t *tx;
    uint8_t       *rx;
    size_t         len;
} io_spi_xfer_t;

/* IO_IOCTL_I2C_WRITE_READ: write wr_len (1 or 2: register address) bytes, repeated
   START, read rd_len bytes from the slave address set with I2C_SET_SLAVE_ADDRESS. */
typedef struct {
    const uint8_t *wr;
    size_t         wr_len;
    uint8_t       *rd;
    size_t         rd_len;
} io_i2c_xfer_t;

typedef struct {
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t rx_overruns;    /* receive buffer/queue full, or the UART ORE flag */
    uint32_t rx_errors;      /* UART framing/noise, I2C NACK or bus error */
    uint32_t timeouts;
} io_stats_t;

/* path: "/NAME/" exactly as in the board table. flags: reserved, must be 0. */
io_handle_t io_open(const char *path, uint32_t flags);
size_t      io_write(io_handle_t h, const void *buf, size_t len);
size_t      io_read(io_handle_t h, void *buf, size_t len);
io_status_t io_ioctl(io_handle_t h, uint32_t request, void *value);

const char *io_status_str(io_status_t st);
const char *io_path(io_handle_t h);

/* Fault and assert paths only: writes s to the board console with interrupts and the
   IO layer bypassed (busy-waits on the data register). */
void io_panic_puts(const char *s);

/* integer ioctl argument helper */
#define IO_VAL(x)  ((void *)(uintptr_t)(x))

#endif /* IO_H */
