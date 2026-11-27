/*
 * SPDX-License-Identifier: MIT
 *
 * UART driver of the IO layer (USART1/2/6 of the STM32F411), 8N1.
 *
 * HAL_UART_Init() sets the baud rate and frame format; data moves on the registers.
 *   TX polled       wait TXE per byte, TC at the end (tx timeout)
 *   TX char queue   bytes go to the TX queue, TXEIE on; the ISR sends one per TXE
 *   TX zero copy    the ISR sends from the caller's buffer and gives the write mutex
 *                   back after the last byte
 *   RX polled       wait RXNE per byte (rx timeout); RXNEIE off
 *   RX circ buffer  RXNEIE on; the ISR stores bytes in the ring
 *   RX char queue   RXNEIE on; the ISR puts bytes in the RX queue
 * Before the scheduler starts every write is polled (the kernel masks the UART IRQs
 * until then). ORE / FE / NE are counted in the statistics; a byte with FE or NE is
 * dropped.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "io_board.h"
#include "io_stm32f4.h"

void USART1_IRQHandler(void);
void USART2_IRQHandler(void);
void USART6_IRQHandler(void);

/* control block per USART for the interrupt handlers (USART1, USART2, USART6) */
static io_peripheral_t *volatile isr_p[3];

static int index_of(const USART_TypeDef *u)
{
    if (u == USART1) {
        return 0;
    }
    if (u == USART2) {
        return 1;
    }
    if (u == USART6) {
        return 2;
    }
    return -1;
}

static void clock_on(const USART_TypeDef *u)
{
    if (u == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
    } else if (u == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE();
    } else {
        __HAL_RCC_USART6_CLK_ENABLE();
    }
}

static const io_uart_hw_t *hw_of(const io_peripheral_t *p)
{
    return (const io_uart_hw_t *)p->desc->hw;
}

static bool flag_txe(void *u)
{
    return (((USART_TypeDef *)u)->SR & USART_SR_TXE) != 0U;
}

static bool flag_tc(void *u)
{
    return (((USART_TypeDef *)u)->SR & USART_SR_TC) != 0U;
}

static bool flag_rxne(void *u)
{
    return (((USART_TypeDef *)u)->SR & USART_SR_RXNE) != 0U;
}

static void cr1_set(USART_TypeDef *u, uint32_t bits, bool on)
{
    taskENTER_CRITICAL();                         /* the ISR changes CR1 too */
    if (on) {
        u->CR1 |= bits;
    } else {
        u->CR1 &= ~bits;
    }
    taskEXIT_CRITICAL();
}

static void kick_tx(void *u)
{
    cr1_set((USART_TypeDef *)u, USART_CR1_TXEIE, true);
}

static io_status_t hal_setup(UART_HandleTypeDef *h, const io_uart_hw_t *c, uint32_t baud)
{
    h->Instance          = c->inst;
    h->Init.BaudRate     = baud;
    h->Init.WordLength   = UART_WORDLENGTH_8B;
    h->Init.StopBits     = UART_STOPBITS_1;
    h->Init.Parity       = UART_PARITY_NONE;
    h->Init.Mode         = UART_MODE_TX_RX;
    h->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    return (HAL_UART_Init(h) == HAL_OK) ? IO_OK : IO_ERR_NOT_READY;
}

/* ---------------------------------------------------------------- driver */

static io_status_t uart_open(io_peripheral_t *p)
{
    const io_uart_hw_t *c = hw_of(p);
    int idx = index_of(c->inst);
    if (idx < 0 || p->priv == NULL) {
        return IO_ERR_NOT_READY;
    }
    io_gpio_clock_on(c->port);
    clock_on(c->inst);
    GPIO_InitTypeDef io = { 0 };
    io.Pin       = (uint32_t)c->tx_pin | c->rx_pin;
    io.Mode      = GPIO_MODE_AF_PP;
    io.Pull      = GPIO_PULLUP;
    io.Speed     = GPIO_SPEED_FREQ_HIGH;
    io.Alternate = c->af;
    HAL_GPIO_Init(c->port, &io);
    if (hal_setup(p->priv, c, c->baud) != IO_OK) {
        return IO_ERR_NOT_READY;
    }
    isr_p[idx] = p;
    HAL_NVIC_SetPriority(c->irq, c->irq_prio, 0U);
    HAL_NVIC_EnableIRQ(c->irq);
    return IO_OK;
}

static size_t write_polled(io_peripheral_t *p, const uint8_t *buf, size_t len)
{
    USART_TypeDef *u = hw_of(p)->inst;
    for (size_t i = 0U; i < len; i++) {
        if (io_polled_wait(flag_txe, u, p->tx_timeout_ms) != IO_OK) {
            p->last = IO_ERR_TIMEOUT;
            return i;
        }
        u->DR = buf[i];
    }
    if (io_polled_wait(flag_tc, u, p->tx_timeout_ms) != IO_OK) {
        p->last = IO_ERR_TIMEOUT;
    }
    return len;
}

static size_t uart_write(io_peripheral_t *p, const uint8_t *buf, size_t len)
{
    USART_TypeDef *u = hw_of(p)->inst;
    io_status_t st = IO_OK;
    size_t n;

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || p->tx_mode == IO_MODE_POLLED) {
        return write_polled(p, buf, len);
    }
    if (p->tx_mode == IO_MODE_CHAR_QUEUE) {
        n = io_cq_write(p->txq, buf, len, p->tx_timeout_ms, kick_tx, u, &st);
        p->last = st;
        return n;
    }
    /* zero copy: io_core checked that the caller holds the write mutex */
    p->zc_next = buf;
    p->zc_left = len;
    io_zc_pass_to_isr(&p->zc);
    kick_tx(u);
    return len;
}

static size_t read_polled(io_peripheral_t *p, uint8_t *buf, size_t len)
{
    USART_TypeDef *u = hw_of(p)->inst;
    for (size_t i = 0U; i < len; i++) {
        if (io_polled_wait(flag_rxne, u, p->rx_timeout_ms) != IO_OK) {
            p->last = IO_ERR_TIMEOUT;
            return i;
        }
        uint32_t sr = u->SR;
        if ((sr & USART_SR_ORE) != 0U) {
            p->stats.rx_overruns++;
        }
        if ((sr & (USART_SR_FE | USART_SR_NE)) != 0U) {
            p->stats.rx_errors++;
        }
        buf[i] = (uint8_t)u->DR;
    }
    return len;
}

static size_t uart_read(io_peripheral_t *p, uint8_t *buf, size_t len)
{
    io_status_t st = IO_OK;
    size_t n;

    switch (p->rx_mode) {
    case IO_MODE_CIRC_BUF:
        n = io_cb_read(p->rxb, buf, len, p->rx_timeout_ms, &st);
        break;
    case IO_MODE_CHAR_QUEUE:
        n = io_cq_read(p->rxq, buf, len, p->rx_timeout_ms, &st);
        break;
    default:
        return read_polled(p, buf, len);
    }
    p->last = st;
    return n;
}

static void uart_mode_changed(io_peripheral_t *p, bool tx)
{
    USART_TypeDef *u = hw_of(p)->inst;
    if (tx) {
        if (p->tx_mode == IO_MODE_POLLED) {
            cr1_set(u, USART_CR1_TXEIE, false);  /* buffered modes switch it on per write */
        }
    } else {
        cr1_set(u, USART_CR1_RXNEIE, p->rx_mode != IO_MODE_POLLED);
    }
}

static io_status_t uart_ioctl(io_peripheral_t *p, uint32_t request, void *value)
{
    const io_uart_hw_t *c = hw_of(p);
    const uint32_t v = (uint32_t)(uintptr_t)value;

    switch (request) {
    case IO_IOCTL_SET_SPEED:
        if (io_zc_held(&p->zc) || (p->tx_mode == IO_MODE_CHAR_QUEUE && io_cq_count(p->txq) != 0U)) {
            return IO_ERR_STATE;                  /* TX still busy */
        }
        if (io_polled_wait(flag_tc, c->inst, p->tx_timeout_ms) != IO_OK) {
            return IO_ERR_TIMEOUT;
        }
        if (hal_setup(p->priv, c, v) != IO_OK) {
            return IO_ERR_IO;
        }
        uart_mode_changed(p, false);              /* HAL_UART_Init leaves RXNEIE alone; be sure */
        return IO_OK;
    case IO_IOCTL_SET_INTERRUPT_PRIORITY:
        HAL_NVIC_SetPriority(c->irq, v, 0U);
        return IO_OK;
    case IO_IOCTL_RELEASE_WRITE_MUTEX:
        return IO_OK;
    default:
        return IO_ERR_UNSUPPORTED;
    }
}

const io_driver_t io_uart_driver = {
    uart_open, uart_write, uart_read, uart_ioctl, uart_mode_changed
};

/* ---------------------------------------------------------------- interrupt */

static void uart_isr(io_peripheral_t *p, USART_TypeDef *u)
{
    BaseType_t woken = pdFALSE;
    uint32_t sr = u->SR;

    /* RX only in an interrupt-driven RX mode: in polled RX mode the task reads DR itself,
       and a TX interrupt must not take its bytes away */
    if ((u->CR1 & USART_CR1_RXNEIE) != 0U &&
        (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        uint8_t b = (uint8_t)u->DR;               /* SR then DR read clears the error flags */
        if (p != NULL) {
            if ((sr & USART_SR_ORE) != 0U) {
                p->stats.rx_overruns++;
            }
            if ((sr & (USART_SR_FE | USART_SR_NE)) != 0U) {
                p->stats.rx_errors++;
            } else if ((sr & USART_SR_RXNE) != 0U) {
                bool kept = true;
                if (p->rx_mode == IO_MODE_CIRC_BUF) {
                    kept = io_cb_put_from_isr(p->rxb, b, &woken);
                } else if (p->rx_mode == IO_MODE_CHAR_QUEUE) {
                    kept = io_cq_put_from_isr(p->rxq, b, &woken);
                }
                if (!kept) {
                    p->stats.rx_overruns++;
                }
            }
        }
    }

    if ((u->CR1 & USART_CR1_TXEIE) != 0U && (sr & USART_SR_TXE) != 0U) {
        uint8_t b;
        if (p != NULL && p->tx_mode == IO_MODE_CHAR_QUEUE && io_cq_get_from_isr(p->txq, &b, &woken)) {
            u->DR = b;
        } else if (p != NULL && p->tx_mode == IO_MODE_ZERO_COPY && p->zc_left > 0U) {
            u->DR = *p->zc_next;
            p->zc_next++;
            if (--p->zc_left == 0U) {
                u->CR1 &= ~USART_CR1_TXEIE;
                io_zc_release_from_isr(&p->zc, &woken);   /* the buffer is free again */
            }
        } else {
            u->CR1 &= ~USART_CR1_TXEIE;            /* nothing left to send */
        }
    }
    portYIELD_FROM_ISR(woken);
}

void USART1_IRQHandler(void)
{
    uart_isr(isr_p[0], USART1);
}

void USART2_IRQHandler(void)
{
    uart_isr(isr_p[1], USART2);
}

void USART6_IRQHandler(void)
{
    uart_isr(isr_p[2], USART6);
}

/* ---------------------------------------------------------------- panic output */

void io_panic_puts(const char *s)
{
    USART_TypeDef *u = IO_BOARD_CONSOLE_UART;

    u->CR1 &= ~USART_CR1_TXEIE;
    while (*s != '\0') {
        while ((u->SR & USART_SR_TXE) == 0U) {
        }
        u->DR = (uint8_t)*s++;
    }
    while ((u->SR & USART_SR_TC) == 0U) {
    }
}
