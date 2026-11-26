/*
 * HAL time base on TIM11 instead of SysTick.
 *
 * Once FreeRTOS runs, SysTick belongs to the kernel. The HAL still needs a 1 ms
 * tick for HAL_GetTick() timeouts, so it gets TIM11 (APB2, 100 MHz timer clock).
 * HAL_RCC_ClockConfig() calls HAL_InitTick() again after the PLL switch, so the
 * prescaler is computed from the live bus clock each time.
 */
#include "stm32f4xx_hal.h"

void TIM1_TRG_COM_TIM11_IRQHandler(void);

static TIM_HandleTypeDef htim_tick;

static uint32_t tim11_clock_hz(void)
{
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    /* APB timers run at 2 x PCLK when the APB prescaler is not 1 */
    return ((RCC->CFGR & RCC_CFGR_PPRE2) == RCC_CFGR_PPRE2_DIV1) ? pclk2 : 2U * pclk2;
}

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    if (TickPriority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }

    __HAL_RCC_TIM11_CLK_ENABLE();

    htim_tick.Instance               = TIM11;
    htim_tick.Init.Prescaler         = (tim11_clock_hz() / 1000000U) - 1U;          /* 1 MHz count */
    htim_tick.Init.Period            = (1000U * (uint32_t)uwTickFreq) - 1U;         /* uwTickFreq in ms */
    htim_tick.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim_tick.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim_tick.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim_tick) != HAL_OK) {
        return HAL_ERROR;
    }

    HAL_NVIC_SetPriority(TIM1_TRG_COM_TIM11_IRQn, TickPriority, 0U);
    HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn);
    uwTickPrio = TickPriority;

    return HAL_TIM_Base_Start_IT(&htim_tick);
}

void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&htim_tick, TIM_IT_UPDATE);
}

void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&htim_tick, TIM_IT_UPDATE);
}

void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim_tick, TIM_FLAG_UPDATE) != RESET) {
        __HAL_TIM_CLEAR_FLAG(&htim_tick, TIM_FLAG_UPDATE);
        HAL_IncTick();
    }
}
