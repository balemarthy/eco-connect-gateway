/*
 * Peripheral I/O adapter in the FreeRTOS+IO style: open / read / write / ioctl by path.
 *
 * SKELETON (make DEMO=plusio). FreeRTOS+IO itself is not used: it exists only for NXP
 * LPC17xx, is GPLv2 and unchanged since V1.0.1 (2012). This adapter keeps its call shape
 * so +IO examples map one to one, but it is our own static code sitting on top of the
 * project drivers, which keep owning the buses (mutexes, chip selects, ISRs):
 *
 *   path      driver                      state
 *   /SPI2/    drivers/spi_bus.c           working: one locked CS cycle per read/write/xfer
 *   /I2C1/    drivers/i2c_bus.c           wired; the driver bodies are the mentee stubs
 *   /UART1/   drivers/uart_link.c         TODO: the protocol task is its only reader/writer
 *   /UART2/   drivers/uart_console.c      TODO: the log task is its only writer
 *
 *   pio_handle_t h = pio_open("/SPI2/", 0);
 *   pio_ioctl(h, PIO_IOCTL_SPI_SET_DEVICE, (void *)&w25q_cs);
 *   pio_spi_xfer_t x = { cmd, 1, id, 3 };
 *   pio_ioctl(h, PIO_IOCTL_SPI_XFER, &x);          // write 1, read 3, one CS cycle
 *
 * Differences from +IO: no malloc (one static descriptor per device, a second open of the
 * same path returns the same descriptor), no transfer-mode ioctls (each driver has one
 * fixed mode), status codes instead of pdPASS/pdFAIL.
 */
#ifndef PLUSIO_H
#define PLUSIO_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PIO_OK = 0,
    PIO_ERR_PARAM,          /* NULL handle, bad ioctl value */
    PIO_ERR_UNSUPPORTED,    /* ioctl not implemented for this device */
    PIO_ERR_BUSY,           /* device owned by another task (TODO: arbitration) */
    PIO_ERR_TIMEOUT,
    PIO_ERR_IO,             /* driver reported an error (NACK, bus error) */
    PIO_ERR_NOT_READY       /* driver not implemented / not initialised */
} pio_status_t;

/* ioctl requests (names follow FreeRTOS_DriverInterface.h where one exists) */
typedef enum {
    PIO_IOCTL_SET_TX_TIMEOUT = 13,     /* value: uint32_t ms (cast to void *) - ioctlSET_TX_TIMEOUT */
    PIO_IOCTL_SET_RX_TIMEOUT = 14,     /* value: uint32_t ms - ioctlSET_RX_TIMEOUT */
    PIO_IOCTL_SET_SPEED = 100,         /* value: uint32_t Hz - ioctlSET_SPEED (TODO, unsupported) */
    PIO_IOCTL_OBTAIN_WRITE_MUTEX = 10, /* value: uint32_t ms - SPI2: spi_bus_lock() */
    PIO_IOCTL_RELEASE_WRITE_MUTEX = 12,/* SPI2: spi_bus_unlock() */
    PIO_IOCTL_I2C_SET_SLAVE_ADDR = 300,/* value: 7-bit address - ioctlSET_I2C_SLAVE_ADDRESS */
    PIO_IOCTL_SPI_SET_DEVICE = 400,    /* value: const spi_device_t * (chip select) - ours */
    PIO_IOCTL_SPI_XFER = 401,          /* value: pio_spi_xfer_t * (write then read, one CS) - ours */
    PIO_IOCTL_GET_LAST_STATUS = 402    /* value: pio_status_t * - status of the last read/write */
} pio_ioctl_t;

typedef struct {
    const uint8_t *tx;
    size_t         tx_len;
    uint8_t       *rx;
    size_t         rx_len;
} pio_spi_xfer_t;

typedef struct pio_desc pio_desc_t;
typedef pio_desc_t *pio_handle_t;

/* NULL if the path is unknown. flags: reserved, pass 0. */
pio_handle_t pio_open(const char *path, uint32_t flags);
/* Bytes transferred (0 on error, see PIO_IOCTL_GET_LAST_STATUS). */
size_t       pio_write(pio_handle_t h, const void *buf, size_t len);
size_t       pio_read(pio_handle_t h, void *buf, size_t len);
pio_status_t pio_ioctl(pio_handle_t h, uint32_t request, void *value);

const char  *pio_status_str(pio_status_t st);

#endif /* PLUSIO_H */
