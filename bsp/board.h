/*
 * NUCLEO-F411RE board definitions: pins, alternate functions, interrupt priorities.
 * One place for the pin map; drivers take their pins from here.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ---- user LED and button ------------------------------------------------ */
#define BOARD_LD2_PORT        GPIOA
#define BOARD_LD2_PIN         GPIO_PIN_5
#define BOARD_B1_PORT         GPIOC
#define BOARD_B1_PIN          GPIO_PIN_13      /* active low */

/* ---- console: USART2 on the ST-LINK virtual COM port --------------------- */
#define BOARD_CONSOLE_UART    USART2
#define BOARD_CONSOLE_PORT    GPIOA
#define BOARD_CONSOLE_TX_PIN  GPIO_PIN_2
#define BOARD_CONSOLE_RX_PIN  GPIO_PIN_3
#define BOARD_CONSOLE_AF      GPIO_AF7_USART2
#define BOARD_CONSOLE_BAUD    115200U

/* ---- SPI2: ENC28J60 + W25Q on one bus, one CS each ------------------------ */
#define BOARD_SPI             SPI2
#define BOARD_SPI_PORT        GPIOB
#define BOARD_SPI_SCK_PIN     GPIO_PIN_13
#define BOARD_SPI_MISO_PIN    GPIO_PIN_14
#define BOARD_SPI_MOSI_PIN    GPIO_PIN_15
#define BOARD_SPI_AF          GPIO_AF5_SPI2

#define BOARD_ENC_CS_PORT     GPIOB
#define BOARD_ENC_CS_PIN      GPIO_PIN_12
#define BOARD_ENC_INT_PORT    GPIOC
#define BOARD_ENC_INT_PIN     GPIO_PIN_4       /* EXTI4, active low */
#define BOARD_ENC_RST_PORT    GPIOC
#define BOARD_ENC_RST_PIN     GPIO_PIN_5       /* active low */

#define BOARD_W25Q_CS_PORT    GPIOB
#define BOARD_W25Q_CS_PIN     GPIO_PIN_1

/* ---- NVIC priorities (0 = highest). Any ISR that calls a FreeRTOS "FromISR"
 *      function must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5). */
#define BOARD_IRQ_PRIO_CONSOLE   7U

/* ---- clocks ---------------------------------------------------------------- */
#define BOARD_SYSCLK_HZ       100000000U       /* HCLK 100 MHz, APB1 50 MHz, APB2 100 MHz */

void board_init(void);
/* Locally administered MAC 02:EC:xx:xx:xx:xx, the low 4 bytes from the 96-bit device UID. */
void board_mac_address(uint8_t mac[6]);
void board_led_set(bool on);
void board_led_toggle(void);
bool board_button_pressed(void);

#endif /* BOARD_H */
