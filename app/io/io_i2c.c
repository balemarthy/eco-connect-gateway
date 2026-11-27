/*
 * SPDX-License-Identifier: MIT
 *
 * I2C master driver of the IO layer (I2C1..I2C3 of the STM32F411), 7-bit addresses,
 * polled HAL transfers with the handle's TX/RX timeout.
 *
 *     io_handle_t i2c = io_open("/I2C1/", 0U);
 *     io_ioctl(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x48));
 *     uint8_t reg = 0x00U, raw[2];
 *     io_i2c_xfer_t x = { &reg, 1U, raw, sizeof raw };
 *     io_ioctl(i2c, IO_IOCTL_I2C_WRITE_READ, &x);       // write reg, repeated START, read 2
 *
 * io_write() / io_read() are a plain write or read to the slave address.
 * NACK -> IO_ERR_IO (counted in rx_errors). Bus error, arbitration lost or a bus that
 * stays busy -> bus recovery (up to 9 SCL pulses until SDA is released, then a STOP),
 * peripheral re-initialised, IO_ERR_IO. In zero-copy mode the bus mutex is needed, as
 * for SPI.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "io_board.h"
#include "io_stm32f4.h"

static void clock_on(const I2C_TypeDef *i)
{
    if (i == I2C1) {
        __HAL_RCC_I2C1_CLK_ENABLE();
    } else if (i == I2C2) {
        __HAL_RCC_I2C2_CLK_ENABLE();
    } else {
        __HAL_RCC_I2C3_CLK_ENABLE();
    }
}

static const io_i2c_hw_t *hw_of(const io_peripheral_t *p)
{
    return (const io_i2c_hw_t *)p->desc->hw;
}

static bool owner_ok(const io_peripheral_t *p)
{
    return p->tx_mode != IO_MODE_ZERO_COPY || io_zc_caller_holds(&p->zc);
}

static void pins_af(const io_i2c_hw_t *c)
{
    GPIO_InitTypeDef io = { 0 };
    io.Pin       = (uint32_t)c->scl_pin | c->sda_pin;
    io.Mode      = GPIO_MODE_AF_OD;
    io.Pull      = GPIO_PULLUP;               /* weak; the bus still needs external pull-ups */
    io.Speed     = GPIO_SPEED_FREQ_HIGH;
    io.Alternate = c->af;
    HAL_GPIO_Init(c->port, &io);
}

static io_status_t hal_setup(I2C_HandleTypeDef *h, const io_i2c_hw_t *c, uint32_t hz)
{
    h->Instance             = c->inst;
    h->Init.ClockSpeed      = hz;
    h->Init.DutyCycle       = I2C_DUTYCYCLE_2;
    h->Init.OwnAddress1     = 0U;
    h->Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    h->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h->Init.OwnAddress2     = 0U;
    h->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    h->Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    return (HAL_I2C_Init(h) == HAL_OK) ? IO_OK : IO_ERR_NOT_READY;
}

static void half_bit(void)
{
    for (volatile uint32_t i = 0U; i < 250U; i++) {   /* ~5 us at 100 MHz: below 100 kHz */
    }
}

/* SCL pulses as GPIO until a slave holding SDA low lets go, then a STOP; re-init. */
static void bus_recover(io_peripheral_t *p)
{
    const io_i2c_hw_t *c = hw_of(p);
    I2C_HandleTypeDef *h = p->priv;
    GPIO_InitTypeDef io = { 0 };

    (void)HAL_I2C_DeInit(h);
    HAL_GPIO_WritePin(c->port, (uint16_t)(c->scl_pin | c->sda_pin), GPIO_PIN_SET);
    io.Pin   = (uint32_t)c->scl_pin | c->sda_pin;
    io.Mode  = GPIO_MODE_OUTPUT_OD;
    io.Pull  = GPIO_PULLUP;
    io.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(c->port, &io);
    for (unsigned i = 0U; i < 9U && HAL_GPIO_ReadPin(c->port, c->sda_pin) == GPIO_PIN_RESET; i++) {
        HAL_GPIO_WritePin(c->port, c->scl_pin, GPIO_PIN_RESET);
        half_bit();
        HAL_GPIO_WritePin(c->port, c->scl_pin, GPIO_PIN_SET);
        half_bit();
    }
    /* STOP: SDA low -> high while SCL is high */
    HAL_GPIO_WritePin(c->port, c->sda_pin, GPIO_PIN_RESET);
    half_bit();
    HAL_GPIO_WritePin(c->port, c->scl_pin, GPIO_PIN_SET);
    half_bit();
    HAL_GPIO_WritePin(c->port, c->sda_pin, GPIO_PIN_SET);
    half_bit();
    pins_af(c);
    (void)hal_setup(h, c, h->Init.ClockSpeed);
}

static io_status_t result(io_peripheral_t *p, HAL_StatusTypeDef r)
{
    I2C_HandleTypeDef *h = p->priv;
    switch (r) {
    case HAL_OK:
        return IO_OK;
    case HAL_TIMEOUT:
        if ((h->ErrorCode & HAL_I2C_ERROR_AF) != 0U) {
            p->stats.rx_errors++;                 /* NACK reported as a timeout by the HAL */
            return IO_ERR_IO;
        }
        bus_recover(p);
        return IO_ERR_TIMEOUT;
    case HAL_ERROR:
        p->stats.rx_errors++;
        if ((h->ErrorCode & (HAL_I2C_ERROR_BERR | HAL_I2C_ERROR_ARLO)) != 0U) {
            bus_recover(p);
        }
        return IO_ERR_IO;
    default:                                      /* HAL_BUSY: bus stuck */
        p->stats.rx_errors++;
        bus_recover(p);
        return IO_ERR_IO;
    }
}

/* ---------------------------------------------------------------- driver */

static io_status_t i2c_open(io_peripheral_t *p)
{
    const io_i2c_hw_t *c = hw_of(p);
    I2C_HandleTypeDef *h = p->priv;             /* this entry's HAL handle (board table) */
    if (h == NULL) {
        return IO_ERR_NOT_READY;
    }
    io_gpio_clock_on(c->port);
    clock_on(c->inst);
    pins_af(c);
    return hal_setup(h, c, c->hz);
}

static size_t i2c_write(io_peripheral_t *p, const uint8_t *buf, size_t len)
{
    if (p->i2c_addr == 0U || len > 0xFFFFU) {
        p->last = (p->i2c_addr == 0U) ? IO_ERR_STATE : IO_ERR_PARAM;
        return 0U;
    }
    HAL_StatusTypeDef r = HAL_I2C_Master_Transmit(p->priv, (uint16_t)(p->i2c_addr << 1), (uint8_t *)(uintptr_t)buf,
                                                  (uint16_t)len, p->tx_timeout_ms);
    p->last = result(p, r);
    return (p->last == IO_OK) ? len : 0U;
}

static size_t i2c_read(io_peripheral_t *p, uint8_t *buf, size_t len)
{
    if (!owner_ok(p)) {
        p->last = IO_ERR_NOT_OWNER;
        return 0U;
    }
    if (p->i2c_addr == 0U || len > 0xFFFFU) {
        p->last = (p->i2c_addr == 0U) ? IO_ERR_STATE : IO_ERR_PARAM;
        return 0U;
    }
    HAL_StatusTypeDef r = HAL_I2C_Master_Receive(p->priv, (uint16_t)(p->i2c_addr << 1), buf, (uint16_t)len,
                                                 p->rx_timeout_ms);
    p->last = result(p, r);
    return (p->last == IO_OK) ? len : 0U;
}

static io_status_t i2c_ioctl(io_peripheral_t *p, uint32_t request, void *value)
{
    const uint32_t v = (uint32_t)(uintptr_t)value;

    if (request == IO_IOCTL_RELEASE_WRITE_MUTEX) {
        return IO_OK;
    }
    if (!owner_ok(p)) {
        return IO_ERR_NOT_OWNER;
    }
    switch (request) {
    case IO_IOCTL_I2C_SET_SLAVE_ADDRESS:
        p->i2c_addr = (uint8_t)v;
        return IO_OK;
    case IO_IOCTL_I2C_WRITE_READ: {
        const io_i2c_xfer_t *x = value;
        if (p->i2c_addr == 0U) {
            return IO_ERR_STATE;
        }
        uint16_t reg = (x->wr_len == 2U) ? (uint16_t)((x->wr[0] << 8) | x->wr[1]) : x->wr[0];
        HAL_StatusTypeDef r = HAL_I2C_Mem_Read(p->priv, (uint16_t)(p->i2c_addr << 1), reg,
                                               (x->wr_len == 2U) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT,
                                               x->rd, (uint16_t)x->rd_len, p->rx_timeout_ms);
        io_status_t st = result(p, r);
        if (st == IO_OK) {
            p->stats.tx_bytes += (uint32_t)x->wr_len;
            p->stats.rx_bytes += (uint32_t)x->rd_len;
        } else if (st == IO_ERR_TIMEOUT) {
            p->stats.timeouts++;
        }
        return st;
    }
    case IO_IOCTL_SET_SPEED: {
        I2C_HandleTypeDef *h = p->priv;
        (void)HAL_I2C_DeInit(h);
        pins_af(hw_of(p));
        return hal_setup(h, hw_of(p), v) == IO_OK ? IO_OK : IO_ERR_IO;
    }
    default:
        return IO_ERR_UNSUPPORTED;
    }
}

const io_driver_t io_i2c_driver = {
    i2c_open, i2c_write, i2c_read, i2c_ioctl, NULL
};
