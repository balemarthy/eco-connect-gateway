/*
 * I2C1 bus driver - contract stubs. See i2c_bus.h for what to implement.
 */
#include "i2c_bus.h"

i2c_status_t i2c_bus_init(void)
{
    /* MENTEE TASK: clocks, PB8/PB9 AF4 open-drain, HAL_I2C_Init at I2C_BUS_SPEED_HZ, mutex. */
    return I2C_ERR_NOT_READY;
}

i2c_status_t i2c_bus_write(uint8_t addr7, const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    (void)addr7;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return I2C_ERR_NOT_READY;
}

i2c_status_t i2c_bus_read(uint8_t addr7, uint8_t *data, size_t len, uint32_t timeout_ms)
{
    (void)addr7;
    (void)data;
    (void)len;
    (void)timeout_ms;
    return I2C_ERR_NOT_READY;
}

i2c_status_t i2c_bus_write_read(uint8_t addr7, const uint8_t *wr, size_t wr_len,
                                uint8_t *rd, size_t rd_len, uint32_t timeout_ms)
{
    (void)addr7;
    (void)wr;
    (void)wr_len;
    (void)rd;
    (void)rd_len;
    (void)timeout_ms;
    return I2C_ERR_NOT_READY;
}
