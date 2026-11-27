/*
 * Board bring-up: GPIO clocks, LED, button, ENC28J60 reset line.
 * UART / SPI / I2C pins and the SPI chip selects are set up by the IO layer drivers
 * from app/io/io_board_f411.h when the peripheral is opened.
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

    /* ENC28J60 hardware reset line, released (high). */
    HAL_GPIO_WritePin(BOARD_ENC_RST_PORT, BOARD_ENC_RST_PIN, GPIO_PIN_SET);
    io.Pin   = BOARD_ENC_RST_PIN;
    io.Mode  = GPIO_MODE_OUTPUT_PP;
    io.Pull  = GPIO_NOPULL;
    io.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_ENC_RST_PORT, &io);
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

void board_delay_busy_ms(uint32_t ms)
{
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U || (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    uint32_t cycles_per_ms = SystemCoreClock / 1000U;
    while (ms-- > 0U) {
        uint32_t t0 = DWT->CYCCNT;
        while ((DWT->CYCCNT - t0) < cycles_per_ms) {
        }
    }
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
