/*
 * Exception and interrupt handlers. Names must match the vector table in
 * bsp/startup_stm32f411retx.s (weak aliases there fall back to Default_Handler).
 *
 * SVC_Handler, PendSV_Handler and SysTick_Handler come from the FreeRTOS port
 * (mapped in app/FreeRTOSConfig.h). The HAL tick runs on TIM11 (hal_timebase_tim.c).
 */
#include "stm32f4xx_hal.h"
#include "stm32f4xx_it.h"

#include "board.h"
#include "enc28j60.h"

void NMI_Handler(void)
{
    for (;;) {
    }
}

/* Stop here so a debugger shows the stacked PC/LR. */
void HardFault_Handler(void)
{
    for (;;) {
    }
}

void MemManage_Handler(void)
{
    for (;;) {
    }
}

void BusFault_Handler(void)
{
    for (;;) {
    }
}

void UsageFault_Handler(void)
{
    for (;;) {
    }
}

void DebugMon_Handler(void)
{
}

/* ENC28J60 INT (PC4, falling edge): only wakes the network task. */
void EXTI4_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(BOARD_ENC_INT_PIN) != 0U) {
        __HAL_GPIO_EXTI_CLEAR_IT(BOARD_ENC_INT_PIN);
        enc28j60_int_isr();
    }
}
