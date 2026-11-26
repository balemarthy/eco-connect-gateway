/*
 * USART1 link driver: interrupt RX into a stream buffer, blocking TX.
 */
#include "FreeRTOS.h"
#include "stream_buffer.h"

#include "board.h"
#include "uart_link.h"

void USART1_IRQHandler(void);

static UART_HandleTypeDef huart_link;
static StaticStreamBuffer_t rx_sb_ctrl;
static uint8_t rx_sb_storage[UART_LINK_RX_BUF + 1U];
static StreamBufferHandle_t rx_sb;
static volatile uint32_t overruns;
static volatile uint32_t errors;

bool uart_link_init(void)
{
    rx_sb = xStreamBufferCreateStatic(UART_LINK_RX_BUF, 1U, rx_sb_storage, &rx_sb_ctrl);
    configASSERT(rx_sb != NULL);

    huart_link.Instance          = BOARD_LINK_UART;
    huart_link.Init.BaudRate     = UART_LINK_BAUD;
    huart_link.Init.WordLength   = UART_WORDLENGTH_8B;
    huart_link.Init.StopBits     = UART_STOPBITS_1;
    huart_link.Init.Parity       = UART_PARITY_NONE;
    huart_link.Init.Mode         = UART_MODE_TX_RX;
    huart_link.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart_link.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart_link) != HAL_OK) {
        return false;
    }
    BOARD_LINK_UART->CR1 |= USART_CR1_RXNEIE;      /* RXNE (ORE is reported with it) */
    HAL_NVIC_SetPriority(USART1_IRQn, BOARD_IRQ_PRIO_LINK, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    return true;
}

void USART1_IRQHandler(void)
{
    USART_TypeDef *u = BOARD_LINK_UART;
    BaseType_t woken = pdFALSE;
    uint32_t sr = u->SR;

    if ((sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) != 0U) {
        uint8_t b = (uint8_t)u->DR;                  /* SR then DR read clears the error flags */
        if ((sr & USART_SR_ORE) != 0U) {
            overruns++;
        }
        if ((sr & (USART_SR_FE | USART_SR_NE)) != 0U) {
            errors++;
        } else if ((sr & USART_SR_RXNE) != 0U) {
            if (xStreamBufferSendFromISR(rx_sb, &b, 1U, &woken) != 1U) {
                overruns++;
            }
        }
    }
    portYIELD_FROM_ISR(woken);
}

size_t uart_link_read(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    return xStreamBufferReceive(rx_sb, buf, max, pdMS_TO_TICKS(timeout_ms));
}

bool uart_link_write(const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    return HAL_UART_Transmit(&huart_link, buf, (uint16_t)len, timeout_ms) == HAL_OK;
}

uint32_t uart_link_rx_overruns(void)
{
    return overruns;
}

uint32_t uart_link_rx_errors(void)
{
    return errors;
}
