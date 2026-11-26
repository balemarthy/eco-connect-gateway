/*
 * I2C1 master bus contract (PB8 SCL / PB9 SDA, 100 kHz standard mode).
 *
 * MENTEE TASK: the bodies in i2c_bus.c are stubs that return I2C_ERR_NOT_READY.
 * Implement them with the STM32F4 HAL (stm32f4xx_hal_i2c.c is already vendored;
 * enable HAL_I2C_MODULE_ENABLED in bsp/stm32f4xx_hal_conf.h and add hal_i2c to
 * HAL_MODULES in the Makefile). Keep the contract:
 *   - addresses are 7-bit (the driver shifts them),
 *   - every call has a timeout and never blocks forever,
 *   - a mutex serialises callers (sensor task today, maybe more later),
 *   - a bus error (BERR/ARLO) or a stuck SDA triggers a bus recovery
 *     (9 SCL pulses, STOP) before the error is returned.
 */
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    I2C_OK = 0,
    I2C_ERR_NACK,        /* address or data not acknowledged */
    I2C_ERR_TIMEOUT,
    I2C_ERR_BUS,         /* bus error / arbitration lost, recovery attempted */
    I2C_ERR_NOT_READY    /* not implemented or not initialised */
} i2c_status_t;

#define I2C_BUS_SPEED_HZ  100000U

i2c_status_t i2c_bus_init(void);
i2c_status_t i2c_bus_write(uint8_t addr7, const uint8_t *data, size_t len, uint32_t timeout_ms);
i2c_status_t i2c_bus_read(uint8_t addr7, uint8_t *data, size_t len, uint32_t timeout_ms);
/* Write then repeated START and read (register reads). */
i2c_status_t i2c_bus_write_read(uint8_t addr7, const uint8_t *wr, size_t wr_len,
                                uint8_t *rd, size_t rd_len, uint32_t timeout_ms);

#endif /* I2C_BUS_H */
