/*
 * Board bring-up: GPIO clocks, LED, button, and the HAL MSP hooks that route
 * peripherals to pins. The pin map itself is in board.h.
 */
#include "board.h"

void board_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef io = {0};

    HAL_GPIO_WritePin(BOARD_LD2_PORT, BOARD_LD2_PIN, GPIO_PIN_RESET);
    io.Pin   = BOARD_LD2_PIN;
    io.Mode  = GPIO_MODE_OUTPUT_PP;
    io.Pull  = GPIO_NOPULL;
    io.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LD2_PORT, &io);

    io.Pin  = BOARD_B1_PIN;
    io.Mode = GPIO_MODE_INPUT;
    io.Pull = GPIO_NOPULL;                         /* external pull-up on the Nucleo */
    HAL_GPIO_Init(BOARD_B1_PORT, &io);

    /* Every SPI chip-select high (deselected) before the bus is clocked, so no
       device sees the first SCK edges as a command. */
    HAL_GPIO_WritePin(BOARD_ENC_CS_PORT, BOARD_ENC_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BOARD_W25Q_CS_PORT, BOARD_W25Q_CS_PIN, GPIO_PIN_SET);
    io.Pin   = BOARD_ENC_CS_PIN | BOARD_W25Q_CS_PIN;
    io.Mode  = GPIO_MODE_OUTPUT_PP;
    io.Pull  = GPIO_NOPULL;
    io.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &io);

    /* ENC28J60 hardware reset line, released (high). */
    HAL_GPIO_WritePin(BOARD_ENC_RST_PORT, BOARD_ENC_RST_PIN, GPIO_PIN_SET);
    io.Pin   = BOARD_ENC_RST_PIN;
    io.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_ENC_RST_PORT, &io);
}

/* Called by HAL_SPI_Init(). */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == BOARD_SPI) {
        __HAL_RCC_SPI2_CLK_ENABLE();
        GPIO_InitTypeDef io = {0};
        io.Pin       = BOARD_SPI_SCK_PIN | BOARD_SPI_MISO_PIN | BOARD_SPI_MOSI_PIN;
        io.Mode      = GPIO_MODE_AF_PP;
        io.Pull      = GPIO_NOPULL;
        io.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        io.Alternate = BOARD_SPI_AF;
        HAL_GPIO_Init(BOARD_SPI_PORT, &io);
    }
}

void board_mac_address(uint8_t mac[6])
{
    uint32_t h = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();

    mac[0] = 0x02U;                 /* locally administered, unicast */
    mac[1] = 0xECU;
    mac[2] = (uint8_t)(h >> 24);
    mac[3] = (uint8_t)(h >> 16);
    mac[4] = (uint8_t)(h >> 8);
    mac[5] = (uint8_t)h;
}

void board_enc_int_enable(void)
{
    GPIO_InitTypeDef io = {0};
    io.Pin  = BOARD_ENC_INT_PIN;
    io.Mode = GPIO_MODE_IT_FALLING;                /* INT is active low */
    io.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BOARD_ENC_INT_PORT, &io);
    __HAL_GPIO_EXTI_CLEAR_IT(BOARD_ENC_INT_PIN);
    HAL_NVIC_SetPriority(EXTI4_IRQn, BOARD_IRQ_PRIO_ENC_INT, 0U);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
}

void board_led_set(bool on)
{
    HAL_GPIO_WritePin(BOARD_LD2_PORT, BOARD_LD2_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(BOARD_LD2_PORT, BOARD_LD2_PIN);
}

bool board_button_pressed(void)
{
    return HAL_GPIO_ReadPin(BOARD_B1_PORT, BOARD_B1_PIN) == GPIO_PIN_RESET;
}

/* Called by HAL_UART_Init(): clocks and pins for each UART we use. */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef io = {0};

    if (huart->Instance == BOARD_CONSOLE_UART) {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        io.Pin       = BOARD_CONSOLE_TX_PIN | BOARD_CONSOLE_RX_PIN;
        io.Mode      = GPIO_MODE_AF_PP;
        io.Pull      = GPIO_PULLUP;
        io.Speed     = GPIO_SPEED_FREQ_HIGH;
        io.Alternate = BOARD_CONSOLE_AF;
        HAL_GPIO_Init(BOARD_CONSOLE_PORT, &io);
    } else if (huart->Instance == BOARD_LINK_UART) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        io.Pin       = BOARD_LINK_TX_PIN | BOARD_LINK_RX_PIN;
        io.Mode      = GPIO_MODE_AF_PP;
        io.Pull      = GPIO_PULLUP;
        io.Speed     = GPIO_SPEED_FREQ_HIGH;
        io.Alternate = BOARD_LINK_AF;
        HAL_GPIO_Init(BOARD_LINK_PORT, &io);
    }
}
