/*
 * Polled console on USART2.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "uart_console.h"

#define CONSOLE_TX_TIMEOUT_MS  100U
#define CONSOLE_FMT_SIZE       160U

static UART_HandleTypeDef huart_console;

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
    return HAL_UART_Init(&huart_console) == HAL_OK;
}

void console_write(const char *buf, size_t len)
{
    if (len > 0U) {
        (void)HAL_UART_Transmit(&huart_console, (const uint8_t *)buf, (uint16_t)len, CONSOLE_TX_TIMEOUT_MS);
    }
}

void console_puts(const char *s)
{
    console_write(s, strlen(s));
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
