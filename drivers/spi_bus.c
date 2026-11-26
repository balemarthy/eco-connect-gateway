/*
 * SPI2 bus driver. HAL_SPI_Init() sets up the peripheral; byte transfers are done
 * on the registers (polled), which is faster than the HAL for the short command
 * sequences the ENC28J60 uses.
 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define LOG_TAG LOG_MOD_SPI
#include "log.h"

#include "board.h"
#include "spi_bus.h"

static SPI_HandleTypeDef hspi;
static StaticSemaphore_t lock_buf;
static SemaphoreHandle_t lock;
static bool ready;

spi_status_t spi_bus_init(void)
{
    hspi.Instance               = BOARD_SPI;
    hspi.Init.Mode              = SPI_MODE_MASTER;
    hspi.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi.Init.NSS               = SPI_NSS_SOFT;
    hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;      /* 50 MHz / 4 = 12.5 MHz */
    hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi.Init.CRCPolynomial     = 7U;
    if (HAL_SPI_Init(&hspi) != HAL_OK) {
        LOG_E("SPI2 init failed");
        return SPI_ERR_NOT_READY;
    }
    __HAL_SPI_ENABLE(&hspi);

    lock = xSemaphoreCreateMutexStatic(&lock_buf);
    configASSERT(lock != NULL);
    ready = true;

    uint32_t hz = spi_bus_clock_hz();
    LOG_I("SPI2 ready, %lu.%lu MHz", (unsigned long)(hz / 1000000U), (unsigned long)((hz / 100000U) % 10U));
    return SPI_OK;
}

uint32_t spi_bus_clock_hz(void)
{
    uint32_t br = (BOARD_SPI->CR1 & SPI_CR1_BR) >> SPI_CR1_BR_Pos;   /* fPCLK / 2^(BR+1) */
    return HAL_RCC_GetPCLK1Freq() >> (br + 1U);
}

spi_status_t spi_bus_lock(uint32_t timeout_ms)
{
    if (!ready) {
        return SPI_ERR_NOT_READY;
    }
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return SPI_OK;                           /* single thread before the scheduler starts */
    }
    return (xSemaphoreTake(lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) ? SPI_OK : SPI_ERR_TIMEOUT;
}

void spi_bus_unlock(void)
{
    if (ready && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        (void)xSemaphoreGive(lock);
    }
}

void spi_dev_select(const spi_device_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

void spi_dev_deselect(const spi_device_t *dev)
{
    /* wait until the last bit has left the shift register before raising CS */
    while ((BOARD_SPI->SR & SPI_SR_BSY) != 0U) {
    }
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

spi_status_t spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    SPI_TypeDef *s = BOARD_SPI;

    if (!ready) {
        return SPI_ERR_NOT_READY;
    }
    for (size_t i = 0; i < len; i++) {
        uint32_t t0 = HAL_GetTick();
        while ((s->SR & SPI_SR_TXE) == 0U) {
            if (HAL_GetTick() - t0 > SPI_BYTE_TIMEOUT_MS) {
                return SPI_ERR_TIMEOUT;
            }
        }
        *(volatile uint8_t *)&s->DR = (tx != NULL) ? tx[i] : 0xFFU;
        while ((s->SR & SPI_SR_RXNE) == 0U) {
            if (HAL_GetTick() - t0 > SPI_BYTE_TIMEOUT_MS) {
                return SPI_ERR_TIMEOUT;
            }
        }
        uint8_t b = *(volatile uint8_t *)&s->DR;
        if (rx != NULL) {
            rx[i] = b;
        }
    }
    return SPI_OK;
}

spi_status_t spi_dev_xfer(const spi_device_t *dev, const uint8_t *tx, size_t tx_len,
                          uint8_t *rx, size_t rx_len, uint32_t timeout_ms)
{
    if (dev == NULL || (tx_len > 0U && tx == NULL)) {
        return SPI_ERR_PARAM;
    }
    spi_status_t st = spi_bus_lock(timeout_ms);
    if (st != SPI_OK) {
        return st;
    }
    spi_dev_select(dev);
    st = spi_bus_transfer(tx, NULL, tx_len);
    if (st == SPI_OK && rx_len > 0U) {
        st = spi_bus_transfer(NULL, rx, rx_len);
    }
    spi_dev_deselect(dev);
    spi_bus_unlock();
    return st;
}
