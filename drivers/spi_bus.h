/*
 * SPI2 master bus shared by the ENC28J60 and the W25Q flash.
 *
 * One mutex per bus, one chip-select per device. A driver that needs several
 * CS cycles in one go (the ENC28J60 does) holds the lock across them:
 *
 *     if (spi_bus_lock(50) == SPI_OK) {
 *         spi_dev_select(&dev);  spi_bus_transfer(cmd, NULL, 1);  spi_dev_deselect(&dev);
 *         ...
 *         spi_bus_unlock();
 *     }
 *
 * spi_dev_xfer() does lock + select + transfer + deselect + unlock for one cycle.
 * Mode 0 (CPOL 0, CPHA 0), MSB first, 8-bit, PCLK1/4 = 12.5 MHz.
 */
#ifndef SPI_BUS_H
#define SPI_BUS_H

#include <stddef.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

typedef enum {
    SPI_OK = 0,
    SPI_ERR_TIMEOUT,     /* bus lock or a byte transfer timed out */
    SPI_ERR_PARAM,
    SPI_ERR_NOT_READY    /* spi_bus_init() not called or failed */
} spi_status_t;

typedef struct {
    const char   *name;
    GPIO_TypeDef *cs_port;
    uint16_t      cs_pin;
} spi_device_t;

#define SPI_BYTE_TIMEOUT_MS  5U

spi_status_t spi_bus_init(void);
uint32_t     spi_bus_clock_hz(void);

spi_status_t spi_bus_lock(uint32_t timeout_ms);
void         spi_bus_unlock(void);

void spi_dev_select(const spi_device_t *dev);
void spi_dev_deselect(const spi_device_t *dev);

/* Full duplex. tx == NULL sends 0xFF, rx == NULL discards. Caller holds the lock. */
spi_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t len);

/* One complete CS cycle: lock, select, write tx_len bytes, read rx_len bytes, deselect, unlock. */
spi_status_t spi_dev_xfer(const spi_device_t *dev, const uint8_t *tx, size_t tx_len,
                          uint8_t *rx, size_t rx_len, uint32_t timeout_ms);

#endif /* SPI_BUS_H */
