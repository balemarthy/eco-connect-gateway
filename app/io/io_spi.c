/*
 * SPDX-License-Identifier: MIT
 *
 * SPI master driver of the IO layer (SPI1..SPI5 of the STM32F411), 8-bit, MSB first,
 * software chip selects from the board table.
 *
 * HAL_SPI_Init() sets the peripheral up; bytes move on the registers (polled), which is
 * faster than the HAL for the short command sequences of the ENC28J60. One transfer
 * runs inside one chip select:
 *
 *     io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(50));      // bus lock (zero copy)
 *     io_ioctl(spi, IO_IOCTL_SPI_SELECT, IO_VAL(IO_SPI2_CS_W25Q));  // CS low
 *     io_write(spi, cmd, 4);  io_read(spi, data, n);               // or IO_IOCTL_SPI_TRANSFER
 *     io_ioctl(spi, IO_IOCTL_SPI_DESELECT, NULL);                  // wait BSY, CS high
 *     io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL);
 *
 * In zero-copy mode (the /SPI2/ default) every transfer, chip select and setting needs
 * the caller to hold the bus mutex; in polled mode the caller guarantees it is alone.
 * io_write() discards what comes back, io_read() sends 0xFF.
 */
#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_SPI
#include "log.h"

#include "io_board.h"
#include "io_stm32f4.h"

#define SPI_BYTE_TIMEOUT_MS  5U

static void clock_on(const SPI_TypeDef *s)
{
    if (s == SPI1) {
        __HAL_RCC_SPI1_CLK_ENABLE();
    } else if (s == SPI2) {
        __HAL_RCC_SPI2_CLK_ENABLE();
    } else if (s == SPI3) {
        __HAL_RCC_SPI3_CLK_ENABLE();
    } else if (s == SPI4) {
        __HAL_RCC_SPI4_CLK_ENABLE();
    } else {
        __HAL_RCC_SPI5_CLK_ENABLE();
    }
}

static const io_spi_hw_t *hw_of(const io_peripheral_t *p)
{
    return (const io_spi_hw_t *)p->desc->hw;
}

static uint32_t pclk_of(const SPI_TypeDef *s)
{
    /* SPI2/SPI3 on APB1, the others on APB2 */
    return (s == SPI2 || s == SPI3) ? HAL_RCC_GetPCLK1Freq() : HAL_RCC_GetPCLK2Freq();
}

/* BR field 0..7: f = pclk / 2^(BR+1); the fastest that does not exceed hz */
static uint32_t br_for(const SPI_TypeDef *s, uint32_t hz)
{
    uint32_t pclk = pclk_of(s);
    uint32_t br = 0U;
    while (br < 7U && (pclk >> (br + 1U)) > hz) {
        br++;
    }
    return br;
}

static uint32_t clock_hz(const SPI_TypeDef *s)
{
    return pclk_of(s) >> (((s->CR1 & SPI_CR1_BR) >> SPI_CR1_BR_Pos) + 1U);
}

static bool owner_ok(const io_peripheral_t *p)
{
    return p->tx_mode != IO_MODE_ZERO_COPY || io_zc_caller_holds(&p->zc);
}

static io_status_t wait_idle(SPI_TypeDef *s)
{
    uint32_t t0 = io_port_ms();
    while ((s->SR & SPI_SR_BSY) != 0U) {
        if (io_port_ms() - t0 > SPI_BYTE_TIMEOUT_MS) {
            return IO_ERR_TIMEOUT;
        }
    }
    return IO_OK;
}

/* full duplex on the registers; tx NULL sends 0xFF, rx NULL discards */
static io_status_t transfer(SPI_TypeDef *s, const uint8_t *tx, uint8_t *rx, size_t len)
{
    for (size_t i = 0U; i < len; i++) {
        uint32_t t0 = io_port_ms();
        while ((s->SR & SPI_SR_TXE) == 0U) {
            if (io_port_ms() - t0 > SPI_BYTE_TIMEOUT_MS) {
                return IO_ERR_TIMEOUT;
            }
        }
        *(volatile uint8_t *)&s->DR = (tx != NULL) ? tx[i] : 0xFFU;
        while ((s->SR & SPI_SR_RXNE) == 0U) {
            if (io_port_ms() - t0 > SPI_BYTE_TIMEOUT_MS) {
                return IO_ERR_TIMEOUT;
            }
        }
        uint8_t b = *(volatile uint8_t *)&s->DR;
        if (rx != NULL) {
            rx[i] = b;
        }
    }
    return IO_OK;
}

/* ---------------------------------------------------------------- driver */

static io_status_t spi_open(io_peripheral_t *p)
{
    const io_spi_hw_t *c = hw_of(p);
    SPI_HandleTypeDef *h = p->priv;             /* this entry's HAL handle (board table) */
    GPIO_InitTypeDef io = { 0 };

    if (h == NULL) {
        return IO_ERR_NOT_READY;
    }
    /* every chip select high (deselected) before the bus is clocked */
    for (uint8_t i = 0U; i < c->cs_count; i++) {
        io_gpio_clock_on(c->cs[i].port);
        HAL_GPIO_WritePin(c->cs[i].port, c->cs[i].pin, GPIO_PIN_SET);
        io.Pin   = c->cs[i].pin;
        io.Mode  = GPIO_MODE_OUTPUT_PP;
        io.Pull  = GPIO_NOPULL;
        io.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(c->cs[i].port, &io);
    }
    io_gpio_clock_on(c->port);
    clock_on(c->inst);
    io.Pin       = (uint32_t)c->sck_pin | c->miso_pin | c->mosi_pin;
    io.Mode      = GPIO_MODE_AF_PP;
    io.Pull      = GPIO_NOPULL;
    io.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    io.Alternate = c->af;
    HAL_GPIO_Init(c->port, &io);

    h->Instance               = c->inst;
    h->Init.Mode              = SPI_MODE_MASTER;
    h->Init.Direction         = SPI_DIRECTION_2LINES;
    h->Init.DataSize          = SPI_DATASIZE_8BIT;
    h->Init.CLKPolarity       = ((c->mode & 2U) != 0U) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
    h->Init.CLKPhase          = ((c->mode & 1U) != 0U) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    h->Init.NSS               = SPI_NSS_SOFT;
    h->Init.BaudRatePrescaler = br_for(c->inst, c->hz) << SPI_CR1_BR_Pos;
    h->Init.FirstBit          = SPI_FIRSTBIT_MSB;
    h->Init.TIMode            = SPI_TIMODE_DISABLE;
    h->Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    h->Init.CRCPolynomial     = 7U;
    if (HAL_SPI_Init(h) != HAL_OK) {
        LOG_E("%s init failed", p->desc->path);
        return IO_ERR_NOT_READY;
    }
    __HAL_SPI_ENABLE(h);

    uint32_t hz = clock_hz(c->inst);
    LOG_I("%s ready, %lu.%lu MHz, %u chip selects", p->desc->path, (unsigned long)(hz / 1000000U),
          (unsigned long)((hz / 100000U) % 10U), (unsigned)c->cs_count);
    return IO_OK;
}

static size_t spi_write(io_peripheral_t *p, const uint8_t *buf, size_t len)
{
    if (p->cs < 0) {
        p->last = IO_ERR_STATE;                   /* IO_IOCTL_SPI_SELECT first */
        return 0U;
    }
    p->last = transfer(hw_of(p)->inst, buf, NULL, len);
    return (p->last == IO_OK) ? len : 0U;
}

static size_t spi_read(io_peripheral_t *p, uint8_t *buf, size_t len)
{
    if (!owner_ok(p)) {
        p->last = IO_ERR_NOT_OWNER;
        return 0U;
    }
    if (p->cs < 0) {
        p->last = IO_ERR_STATE;
        return 0U;
    }
    p->last = transfer(hw_of(p)->inst, NULL, buf, len);
    return (p->last == IO_OK) ? len : 0U;
}

static void deselect(io_peripheral_t *p)
{
    const io_spi_hw_t *c = hw_of(p);
    (void)wait_idle(c->inst);                     /* last bit out of the shift register */
    if (p->cs >= 0) {
        HAL_GPIO_WritePin(c->cs[p->cs].port, c->cs[p->cs].pin, GPIO_PIN_SET);
    }
    p->cs = -1;
}

static io_status_t reconfigure(io_peripheral_t *p, uint32_t mask, uint32_t bits)
{
    SPI_TypeDef *s = hw_of(p)->inst;
    if (p->cs >= 0) {
        return IO_ERR_STATE;                      /* not in the middle of a transfer */
    }
    if (wait_idle(s) != IO_OK) {
        return IO_ERR_TIMEOUT;
    }
    s->CR1 &= ~SPI_CR1_SPE;
    s->CR1 = (s->CR1 & ~mask) | bits;
    s->CR1 |= SPI_CR1_SPE;
    return IO_OK;
}

static io_status_t spi_ioctl(io_peripheral_t *p, uint32_t request, void *value)
{
    const io_spi_hw_t *c = hw_of(p);
    const uint32_t v = (uint32_t)(uintptr_t)value;

    if (request == IO_IOCTL_RELEASE_WRITE_MUTEX) {
        if (p->cs >= 0) {
            deselect(p);                          /* never leave a chip selected */
        }
        return IO_OK;
    }
    if (!owner_ok(p)) {
        return IO_ERR_NOT_OWNER;
    }
    switch (request) {
    case IO_IOCTL_SPI_SELECT:
        if (p->cs >= 0 && p->cs != (int8_t)v) {
            return IO_ERR_STATE;                  /* another chip is still selected */
        }
        p->cs = (int8_t)v;
        HAL_GPIO_WritePin(c->cs[v].port, c->cs[v].pin, GPIO_PIN_RESET);
        return IO_OK;
    case IO_IOCTL_SPI_DESELECT:
        deselect(p);
        return IO_OK;
    case IO_IOCTL_SPI_TRANSFER: {
        const io_spi_xfer_t *x = value;
        if (p->cs < 0) {
            return IO_ERR_STATE;
        }
        io_status_t st = transfer(c->inst, x->tx, x->rx, x->len);
        if (st == IO_OK) {
            p->stats.tx_bytes += (uint32_t)x->len;
            p->stats.rx_bytes += (uint32_t)x->len;
        } else {
            p->stats.timeouts++;
        }
        return st;
    }
    case IO_IOCTL_SET_SPEED:
        return reconfigure(p, SPI_CR1_BR, br_for(c->inst, v) << SPI_CR1_BR_Pos);
    case IO_IOCTL_SPI_SET_MODE:
        return reconfigure(p, SPI_CR1_CPOL | SPI_CR1_CPHA,
                           (((v & 2U) != 0U) ? SPI_CR1_CPOL : 0U) | (((v & 1U) != 0U) ? SPI_CR1_CPHA : 0U));
    default:
        return IO_ERR_UNSUPPORTED;
    }
}

const io_driver_t io_spi_driver = {
    spi_open, spi_write, spi_read, spi_ioctl, NULL
};
