/*
 * NUCLEO-F411RE board definitions that are not IO-layer peripherals: LED, button,
 * the ENC28J60 interrupt and reset lines, the EXTI priority, clocks.
 * UART, SPI and I2C pins, chip selects and their IRQ priorities are in the IO board
 * table, app/io/io_board_f411.h.
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

/* ---- ENC28J60 control lines (its SPI2 chip select is in io_board_f411.h) -- */
#define BOARD_ENC_INT_PORT    GPIOC
#define BOARD_ENC_INT_PIN     GPIO_PIN_4       /* EXTI4, active low */
#define BOARD_ENC_RST_PORT    GPIOC
#define BOARD_ENC_RST_PIN     GPIO_PIN_5       /* active low */

/* ---- NVIC priorities (0 = highest). Any ISR that calls a FreeRTOS "FromISR"
 *      function must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5).
 *      USART1 5 and USART2 7 are set in io_board_f411.h. */
#define BOARD_IRQ_PRIO_ENC_INT   6U

/* ---- clocks ---------------------------------------------------------------- */
#define BOARD_SYSCLK_HZ       100000000U       /* HCLK 100 MHz, APB1 50 MHz, APB2 100 MHz */

void board_init(void);
/* Locally administered MAC 02:EC:xx:xx:xx:xx, the low 4 bytes from the 96-bit device UID. */
void board_mac_address(uint8_t mac[6]);
/* Busy wait on the DWT cycle counter. Needs no interrupt, so it also works between
   the first FreeRTOS call and vTaskStartScheduler(), where the kernel keeps BASEPRI
   raised and the HAL tick (TIM11) cannot run: HAL_Delay() would never return there. */
void board_delay_busy_ms(uint32_t ms);
/* PC4 as EXTI4 falling edge with pull-up, NVIC on. */
void board_enc_int_enable(void);
void board_led_set(bool on);
void board_led_toggle(void);
bool board_button_pressed(void);

#endif /* BOARD_H */
