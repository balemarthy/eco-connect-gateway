/*
 * Exception and interrupt handlers. Names must match the vector table in
 * bsp/startup_stm32f411retx.s (weak aliases there fall back to Default_Handler).
 */
#include "stm32f4xx_hal.h"
#include "stm32f4xx_it.h"

void NMI_Handler(void)
{
    for (;;) {
    }
}

/* Stop here so a debugger shows the stacked PC/LR. A fault reporter comes later. */
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

/* Step 02: SysTick is the HAL 1 ms tick (HAL_Delay, timeouts). */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
