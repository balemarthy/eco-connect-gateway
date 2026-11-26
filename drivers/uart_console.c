/*
 * Console on USART2.
 *
 * Before the scheduler starts: polled (HAL_UART_Transmit).
 * After: interrupt-driven TX. console_write() copies into a ring buffer and enables
 * TXE; USART2_IRQHandler moves bytes to DR. If the ring is full the writer sleeps
 * until the ISR frees space. Single writer by design (the log task), so the ring
 * has one producer (task) and one consumer (ISR) and needs no lock.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "uart_console.h"

#define CONSOLE_TX_TIMEOUT_MS  100U
#define CONSOLE_FMT_SIZE       160U
#define CONSOLE_RING_SIZE      512U         /* power of two */

void USART2_IRQHandler(void);

static UART_HandleTypeDef huart_console;

static uint8_t           ring[CONSOLE_RING_SIZE];
static volatile uint16_t ring_head;          /* written by the task */
static volatile uint16_t ring_tail;          /* written by the ISR  */
static TaskHandle_t volatile tx_waiter;

bool console_init(void)
{
    huart_console.Instance          = BOARD_CONSOLE_UART;
    huart_console.Init.BaudRate     = BOARD_CONSOLE_BAUD;
    huart_console.Init.WordLength   = UART_WORDLENGTH_8B;
    huart_console.Init.StopBits     = UART_STOPBITS_1;
    huart_console.Init.Parity       = UART_PARITY_NONE;
    huart_console.Init.Mode         = UART_MODE_TX_RX;
    huart_console.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart_console.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart_console) != HAL_OK) {
        return false;
    }
    HAL_NVIC_SetPriority(USART2_IRQn, BOARD_IRQ_PRIO_CONSOLE, 0U);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    return true;
}

static void ring_put(uint8_t c)
{
    uint16_t next = (uint16_t)((ring_head + 1U) & (CONSOLE_RING_SIZE - 1U));

    while (next == ring_tail) {
        /* full: wait for the ISR to drain some bytes (timeout covers a lost wake-up) */
        tx_waiter = xTaskGetCurrentTaskHandle();
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5U));
    }
    ring[ring_head] = c;
    ring_head = next;
}

void console_write(const char *buf, size_t len)
{
    if (len == 0U) {
        return;
    }
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        (void)HAL_UART_Transmit(&huart_console, (const uint8_t *)buf, (uint16_t)len, CONSOLE_TX_TIMEOUT_MS);
        return;
    }
    for (size_t i = 0; i < len; i++) {
        ring_put((uint8_t)buf[i]);
        if ((i & 0x1FU) == 0x1FU || i + 1U == len) {
            taskENTER_CRITICAL();
            BOARD_CONSOLE_UART->CR1 |= USART_CR1_TXEIE;
            taskEXIT_CRITICAL();
        }
    }
}

void USART2_IRQHandler(void)
{
    USART_TypeDef *u = BOARD_CONSOLE_UART;
    BaseType_t woken = pdFALSE;

    if ((u->SR & USART_SR_TXE) != 0U && (u->CR1 & USART_CR1_TXEIE) != 0U) {
        if (ring_tail != ring_head) {
            u->DR = ring[ring_tail];
            ring_tail = (uint16_t)((ring_tail + 1U) & (CONSOLE_RING_SIZE - 1U));
        } else {
            u->CR1 &= ~USART_CR1_TXEIE;
        }
        TaskHandle_t w = tx_waiter;
        if (w != NULL) {
            tx_waiter = NULL;
            vTaskNotifyGiveFromISR(w, &woken);
        }
    }
    /* RX is not used yet: clear overrun so it cannot hold the line. */
    if ((u->SR & USART_SR_ORE) != 0U) {
        (void)u->DR;
    }
    portYIELD_FROM_ISR(woken);
}

void console_puts(const char *s)
{
    console_write(s, strlen(s));
}

void console_panic(const char *s)
{
    USART_TypeDef *u = BOARD_CONSOLE_UART;

    u->CR1 &= ~USART_CR1_TXEIE;
    while (*s != '\0') {
        while ((u->SR & USART_SR_TXE) == 0U) {
        }
        u->DR = (uint8_t)*s++;
    }
    while ((u->SR & USART_SR_TC) == 0U) {
    }
}

void console_printf(const char *fmt, ...)
{
    static char buf[CONSOLE_FMT_SIZE];
    va_list ap;

    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    size_t len = ((size_t)n < sizeof buf) ? (size_t)n : sizeof buf - 1U;
    console_write(buf, len);
}
