/*
 * Exception and interrupt handlers. Names must match the vector table in
 * bsp/startup_stm32f411retx.s (weak aliases there fall back to Default_Handler).
 *
 * SVC_Handler, PendSV_Handler and SysTick_Handler come from the FreeRTOS port
 * (mapped in app/FreeRTOSConfig.h). The HAL tick runs on TIM11 (hal_timebase_tim.c).
 */
#include "stm32f4xx_hal.h"
#include "stm32f4xx_it.h"

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
