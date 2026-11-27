/*
 * SPDX-License-Identifier: MIT
 *
 * STM32F4 part of the IO layer: the hardware description each driver reads from its
 * board-table entry (io_board_f411.h fills them in), the three driver objects, and a
 * GPIO clock helper. Drivers take every pin, IRQ and default from these structs, never
 * from a fixed peripheral, so one driver serves every instance of its kind.
 */
#ifndef IO_STM32F4_H
#define IO_STM32F4_H

#include "stm32f4xx_hal.h"

#include "io_driver.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} io_pin_t;

typedef struct {
    USART_TypeDef *inst;
    IRQn_Type      irq;
    uint8_t        irq_prio;       /* IO_IRQ_PRIO_MIN..MAX (calls FromISR functions) */
    uint32_t       baud;           /* default, 8N1 */
    GPIO_TypeDef  *port;
    uint16_t       tx_pin;
    uint16_t       rx_pin;
    uint8_t        af;
} io_uart_hw_t;

typedef struct {
    SPI_TypeDef    *inst;
    uint32_t        hz;            /* default clock; the next lower prescaler step is used */
    uint8_t         mode;          /* 0..3: CPOL << 1 | CPHA */
    GPIO_TypeDef   *port;
    uint16_t        sck_pin;
    uint16_t        miso_pin;
    uint16_t        mosi_pin;
    uint8_t         af;
    const io_pin_t *cs;            /* chip selects, index = IO_IOCTL_SPI_SELECT value */
    uint8_t         cs_count;
} io_spi_hw_t;

typedef struct {
    I2C_TypeDef  *inst;
    uint32_t      hz;              /* default bus clock (100 kHz standard, up to 400 kHz) */
    GPIO_TypeDef *port;
    uint16_t      scl_pin;
    uint16_t      sda_pin;
    uint8_t       af;
} io_i2c_hw_t;

extern const io_driver_t io_uart_driver;
extern const io_driver_t io_spi_driver;
extern const io_driver_t io_i2c_driver;

static inline void io_gpio_clock_on(const GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOH) {
        __HAL_RCC_GPIOH_CLK_ENABLE();
    }
}

#endif /* IO_STM32F4_H */
