/*
 * SPDX-License-Identifier: MIT
 *
 * Board descriptor table for the NUCLEO-F411RE (STM32F411RE, HCLK 100 MHz, APB1 50 MHz,
 * APB2 100 MHz). Everything the IO layer knows about this board is here: which paths
 * exist, which driver serves each, the pins / IRQ / priority / default speed, the
 * default transfer modes and timeouts, and how much static memory each peripheral gets
 * for the buffered modes. io_board.c turns IO_BOARD_DEVICES into the table.
 *
 * Another board = another io_board_<name>.h with the same macros, selected in io_board.h.
 */
#ifndef IO_BOARD_F411_H
#define IO_BOARD_F411_H

#include "io_stm32f4.h"

#define IO_BOARD_NAME          "NUCLEO-F411RE"

/* io_panic_puts() target and the path the log task writes to */
#define IO_BOARD_CONSOLE_UART  USART2
#define IO_BOARD_CONSOLE_PATH  "/UART2/"

/* ---- /UART1/: framed serial link, PA9 TX / PA10 RX (Arduino D8 / D2), 115200 8N1 ------ */
#define IO_BOARD_UART1_HW  { USART1, USART1_IRQn, 5U, 115200U, GPIOA, GPIO_PIN_9, GPIO_PIN_10, GPIO_AF7_USART1 }

/* ---- /UART2/: console on the ST-LINK virtual COM port, PA2 TX / PA3 RX, 115200 8N1 ----- */
#define IO_BOARD_UART2_HW  { USART2, USART2_IRQn, 7U, 115200U, GPIOA, GPIO_PIN_2, GPIO_PIN_3, GPIO_AF7_USART2 }

/* ---- /SPI2/: ENC28J60 + W25Q on one bus, PB13 SCK / PB14 MISO / PB15 MOSI ------------- */
/* chip selects, index = IO_IOCTL_SPI_SELECT value; all are driven high before the bus
   is clocked, so no device sees the first SCK edges as a command */
enum {
    IO_SPI2_CS_ENC28J60 = 0,     /* PB12 */
    IO_SPI2_CS_W25Q,             /* PB1  */
    IO_SPI2_CS_COUNT
};
#define IO_BOARD_SPI2_CS_PINS  { { GPIOB, GPIO_PIN_12 }, { GPIOB, GPIO_PIN_1 } }
/* mode 0, 12.5 MHz (APB1 50 MHz / 4): ENC28J60 max 20 MHz, W25Q far above */
#define IO_BOARD_SPI2_HW   { SPI2, 12500000U, 0U, GPIOB, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15, \
                             GPIO_AF5_SPI2, io_spi2_cs, IO_SPI2_CS_COUNT }

/* ---- /I2C1/: temperature sensor, PB8 SCL / PB9 SDA (Arduino D15 / D14), 100 kHz -------- */
#define IO_BOARD_I2C1_HW   { I2C1, 100000U, GPIOB, GPIO_PIN_8, GPIO_PIN_9, GPIO_AF4_I2C1 }

/* objects the HW initialisers above refer to (expanded once, in io_board.c) */
#define IO_BOARD_DEFINE_OBJECTS() \
    static const io_pin_t io_spi2_cs[IO_SPI2_CS_COUNT] = IO_BOARD_SPI2_CS_PINS;

/*
 * The descriptor table. Columns:
 *   id, path, type, driver, hw struct type, hw initialiser, driver state type (HAL handle),
 *   TX mode at open, RX mode at open, TX length, RX length (for a buffered mode at open),
 *   TX timeout ms, RX timeout ms, TX reserve, RX reserve (static bytes for the buffered
 *   modes; 0 = mode not available), chip selects
 *
 *  /UART1/  RX circular buffer (the RX interrupt fills it, the protocol task reads it),
 *           TX polled (one writer, frames of < 0.5 KB)
 *  /UART2/  TX character queue (the log task copies a line and goes on; the TXE interrupt
 *           sends it), RX polled; 64 bytes reserved so DEMO=plusio can read keys in
 *           character-queue mode. Before the scheduler every UART write is polled.
 *  /SPI2/   zero copy = bus mutex: each driver obtains it, selects its chip, transfers
 *           from its own buffers and releases it (ENC28J60 and W25Q share the bus)
 *  /I2C1/   polled, one user (the sensor task); switch to zero copy for a second device
 */
#define IO_BOARD_DEVICES(X) \
    X(UART1, "/UART1/", IO_TYPE_UART, io_uart_driver, io_uart_hw_t, IO_BOARD_UART1_HW, UART_HandleTypeDef, \
      IO_MODE_POLLED,     IO_MODE_CIRC_BUF, 0U,   512U, 100U,  1000U, 0U,   512U, 0U) \
    X(UART2, "/UART2/", IO_TYPE_UART, io_uart_driver, io_uart_hw_t, IO_BOARD_UART2_HW, UART_HandleTypeDef, \
      IO_MODE_CHAR_QUEUE, IO_MODE_POLLED,   512U, 0U,   1000U, 100U,  512U, 64U,  0U) \
    X(SPI2,  "/SPI2/",  IO_TYPE_SPI,  io_spi_driver,  io_spi_hw_t,  IO_BOARD_SPI2_HW,  SPI_HandleTypeDef, \
      IO_MODE_ZERO_COPY,  IO_MODE_POLLED,   0U,   0U,   50U,   50U,   0U,   0U,   IO_SPI2_CS_COUNT) \
    X(I2C1,  "/I2C1/",  IO_TYPE_I2C,  io_i2c_driver,  io_i2c_hw_t,  IO_BOARD_I2C1_HW,  I2C_HandleTypeDef, \
      IO_MODE_POLLED,     IO_MODE_POLLED,   0U,   0U,   100U,  100U,  0U,   0U,   0U)

#endif /* IO_BOARD_F411_H */
