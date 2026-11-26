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
    }
}
