/*
 * Framed serial link on USART1 (PA9 TX, PA10 RX, 115200 8N1).
 *
 * RX: the RXNE interrupt pushes every byte into a static stream buffer; one task
 * reads it with uart_link_read(). Bytes that do not fit are counted, not blocked on.
 * TX: blocking with a timeout, for a single writer (the protocol task).
 */
#ifndef UART_LINK_H
#define UART_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UART_LINK_BAUD     115200U
#define UART_LINK_RX_BUF   512U

bool     uart_link_init(void);
size_t   uart_link_read(uint8_t *buf, size_t max, uint32_t timeout_ms);
bool     uart_link_write(const uint8_t *buf, size_t len, uint32_t timeout_ms);
uint32_t uart_link_rx_overruns(void);    /* stream buffer full or UART ORE */
uint32_t uart_link_rx_errors(void);      /* framing / noise errors */

#endif /* UART_LINK_H */
