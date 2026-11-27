/*
 * open/read/write/ioctl adapter over the project drivers (see plusio.h).
 * SKELETON: SPI2 is complete, I2C1 forwards to the stubbed driver, the UARTs are TODO.
 */
#include <string.h>

#include "i2c_bus.h"
#include "plusio.h"
#include "spi_bus.h"

#define PIO_DEFAULT_TIMEOUT_MS  100U

typedef enum { PIO_DEV_SPI, PIO_DEV_I2C, PIO_DEV_UART } pio_type_t;

struct pio_desc {
    const char         *path;
    pio_type_t          type;
    uint32_t            tx_timeout_ms;
    uint32_t            rx_timeout_ms;
    const spi_device_t *spi_dev;        /* SPI: chip select for read/write */
    uint8_t             i2c_addr;       /* I2C: 7-bit target address */
    uint8_t             lock_held;      /* SPI: OBTAIN_WRITE_MUTEX taken by the caller */
    pio_status_t        last;
};

/* One static descriptor per device; the table is the board's device list
   (boardAVAILABLE_DEVICES_LIST in +IO). */
static pio_desc_t devices[] = {
    { "/SPI2/",  PIO_DEV_SPI,  PIO_DEFAULT_TIMEOUT_MS, PIO_DEFAULT_TIMEOUT_MS, NULL, 0U, 0U, PIO_OK },
    { "/I2C1/",  PIO_DEV_I2C,  PIO_DEFAULT_TIMEOUT_MS, PIO_DEFAULT_TIMEOUT_MS, NULL, 0U, 0U, PIO_OK },
    { "/UART1/", PIO_DEV_UART, PIO_DEFAULT_TIMEOUT_MS, PIO_DEFAULT_TIMEOUT_MS, NULL, 0U, 0U, PIO_OK },
    { "/UART2/", PIO_DEV_UART, PIO_DEFAULT_TIMEOUT_MS, PIO_DEFAULT_TIMEOUT_MS, NULL, 0U, 0U, PIO_OK },
};

static pio_status_t from_spi(spi_status_t st)
{
    switch (st) {
    case SPI_OK:            return PIO_OK;
    case SPI_ERR_TIMEOUT:   return PIO_ERR_TIMEOUT;
    case SPI_ERR_PARAM:     return PIO_ERR_PARAM;
    default:                return PIO_ERR_NOT_READY;
    }
}

static pio_status_t from_i2c(i2c_status_t st)
{
    switch (st) {
    case I2C_OK:            return PIO_OK;
    case I2C_ERR_TIMEOUT:   return PIO_ERR_TIMEOUT;
    case I2C_ERR_NACK:
    case I2C_ERR_BUS:       return PIO_ERR_IO;
    default:                return PIO_ERR_NOT_READY;
    }
}

/* One SPI CS cycle: write tx, then read rx. Uses the caller's lock if it holds one. */
static pio_status_t spi_cycle(pio_desc_t *d, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len)
{
    if (d->spi_dev == NULL) {
        return PIO_ERR_PARAM;                        /* PIO_IOCTL_SPI_SET_DEVICE first */
    }
    if (!d->lock_held) {
        return from_spi(spi_dev_xfer(d->spi_dev, tx, tx_len, rx, rx_len, d->tx_timeout_ms));
    }
    spi_status_t st = SPI_OK;
    spi_dev_select(d->spi_dev);
    if (tx_len > 0U) {
        st = spi_bus_transfer(tx, NULL, tx_len);
    }
    if (st == SPI_OK && rx_len > 0U) {
        st = spi_bus_transfer(NULL, rx, rx_len);
    }
    spi_dev_deselect(d->spi_dev);
    return from_spi(st);
}

pio_handle_t pio_open(const char *path, uint32_t flags)
{
    (void)flags;
    if (path == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof devices / sizeof devices[0]; i++) {
        if (strcmp(path, devices[i].path) == 0) {
            pio_desc_t *d = &devices[i];
            if (d->type == PIO_DEV_I2C) {
                d->last = from_i2c(i2c_bus_init());  /* stub today: PIO_ERR_NOT_READY */
            } else {
                d->last = PIO_OK;                    /* SPI2 is brought up by main() */
            }
            return d;
        }
    }
    return NULL;
}

size_t pio_write(pio_handle_t h, const void *buf, size_t len)
{
    if (h == NULL || (buf == NULL && len > 0U)) {
        return 0U;
    }
    switch (h->type) {
    case PIO_DEV_SPI:
        h->last = spi_cycle(h, buf, len, NULL, 0U);
        break;
    case PIO_DEV_I2C:
        h->last = from_i2c(i2c_bus_write(h->i2c_addr, buf, len, h->tx_timeout_ms));
        break;
    default:
        /* TODO /UART1/: uart_link_write() has one writer, the protocol task; add a TX mutex
           in uart_link.c before a second writer may use it. /UART2/: the log task owns TX. */
        h->last = PIO_ERR_BUSY;
        break;
    }
    return (h->last == PIO_OK) ? len : 0U;
}

size_t pio_read(pio_handle_t h, void *buf, size_t len)
{
    if (h == NULL || (buf == NULL && len > 0U)) {
        return 0U;
    }
    switch (h->type) {
    case PIO_DEV_SPI:
        h->last = spi_cycle(h, NULL, 0U, buf, len);
        break;
    case PIO_DEV_I2C:
        h->last = from_i2c(i2c_bus_read(h->i2c_addr, buf, len, h->rx_timeout_ms));
        break;
    default:
        /* TODO /UART1/: the RX stream buffer has a single reader (protocol task); a second
           reader needs the protocol task to hand the port over (e.g. a "raw mode" command). */
        h->last = PIO_ERR_BUSY;
        break;
    }
    return (h->last == PIO_OK) ? len : 0U;
}

pio_status_t pio_ioctl(pio_handle_t h, uint32_t request, void *value)
{
    if (h == NULL) {
        return PIO_ERR_PARAM;
    }
    switch (request) {
    case PIO_IOCTL_SET_TX_TIMEOUT:
        h->tx_timeout_ms = (uint32_t)(uintptr_t)value;
        return PIO_OK;
    case PIO_IOCTL_SET_RX_TIMEOUT:
        h->rx_timeout_ms = (uint32_t)(uintptr_t)value;
        return PIO_OK;
    case PIO_IOCTL_GET_LAST_STATUS:
        if (value == NULL) {
            return PIO_ERR_PARAM;
        }
        *(pio_status_t *)value = h->last;
        return PIO_OK;
    case PIO_IOCTL_SET_SPEED:
        /* TODO: SPI2 runs at PCLK1/4 (12.5 MHz) and I2C1 at 100 kHz, fixed in the drivers. */
        return PIO_ERR_UNSUPPORTED;
    default:
        break;
    }

    if (h->type == PIO_DEV_SPI) {
        switch (request) {
        case PIO_IOCTL_SPI_SET_DEVICE:
            h->spi_dev = (const spi_device_t *)value;
            return (value != NULL) ? PIO_OK : PIO_ERR_PARAM;
        case PIO_IOCTL_SPI_XFER: {
            const pio_spi_xfer_t *x = (const pio_spi_xfer_t *)value;
            if (x == NULL) {
                return PIO_ERR_PARAM;
            }
            h->last = spi_cycle(h, x->tx, x->tx_len, x->rx, x->rx_len);
            return h->last;
        }
        case PIO_IOCTL_OBTAIN_WRITE_MUTEX: {
            pio_status_t st = from_spi(spi_bus_lock((uint32_t)(uintptr_t)value));
            h->lock_held = (st == PIO_OK) ? 1U : 0U;
            return st;
        }
        case PIO_IOCTL_RELEASE_WRITE_MUTEX:
            if (h->lock_held) {
                h->lock_held = 0U;
                spi_bus_unlock();
            }
            return PIO_OK;
        default:
            return PIO_ERR_UNSUPPORTED;
        }
    }
    if (h->type == PIO_DEV_I2C && request == PIO_IOCTL_I2C_SET_SLAVE_ADDR) {
        uint32_t a = (uint32_t)(uintptr_t)value;
        if (a > 0x7FU) {
            return PIO_ERR_PARAM;
        }
        h->i2c_addr = (uint8_t)a;
        return PIO_OK;
    }
    return PIO_ERR_UNSUPPORTED;
}

const char *pio_status_str(pio_status_t st)
{
    static const char *const names[] = {
        "ok", "bad parameter", "unsupported", "busy", "timeout", "io error", "not ready"
    };
    return ((unsigned)st < sizeof names / sizeof names[0]) ? names[st] : "?";
}
