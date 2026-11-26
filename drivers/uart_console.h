/*
 * Console on USART2 (ST-LINK virtual COM port, 115200 8N1).
 *
 * Polled before the scheduler starts, interrupt-driven TX afterwards.
 * After the scheduler starts there is a single writer: the log task. Everything
 * else prints through the log module (app/log/log.h).
 */
#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>

bool console_init(void);
void console_write(const char *buf, size_t len);
void console_puts(const char *s);
/* Formats into a fixed buffer (truncates). Boot and diagnostics only, not the data path. */
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Busy-waits on the data register; safe with interrupts off (fault and assert paths). */
void console_panic(const char *s);

#endif /* UART_CONSOLE_H */
