/*
 * Step 01: bare-metal register blinky.
 *
 * No HAL, no RTOS. After reset the core runs from the 16 MHz HSI.
 * SysTick is used as a polled 1 ms time base (COUNTFLAG), LD2 (PA5) toggles every 500 ms.
 */
#include <stdint.h>
#include "stm32f4xx.h"

#define LD2_PIN        5U
#define HSI_HZ         16000000U
#define BLINK_HALF_MS  500U

/* Called by Reset_Handler before main(). For now only the FPU is enabled:
   the code is built with -mfloat-abi=hard, so any float instruction would fault
   with CP10/CP11 still disabled. */
void SystemInit(void)
{
    SCB->CPACR |= (3UL << 20) | (3UL << 22);
    __DSB();
    __ISB();
}

static void systick_1ms_init(void)
{
    SysTick->LOAD = (HSI_HZ / 1000U) - 1U;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;  /* no interrupt */
}

static void delay_ms(uint32_t ms)
{
    while (ms-- > 0U) {
        /* COUNTFLAG is set when the counter wraps and cleared by reading CTRL */
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0U) {
        }
    }
}

static void ld2_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;                         /* read back: clock is on before the first GPIO access */

    GPIOA->MODER   = (GPIOA->MODER & ~GPIO_MODER_MODER5_Msk) | (1U << GPIO_MODER_MODER5_Pos);  /* output */
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;                                                         /* push-pull */
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5_Msk;                                               /* low speed */
    GPIOA->PUPDR  &= ~GPIO_PUPDR_PUPD5_Msk;                                                    /* no pull */
}

int main(void)
{
    ld2_init();
    systick_1ms_init();

    for (;;) {
        GPIOA->BSRR = (1UL << LD2_PIN);             /* set:   LED on  */
        delay_ms(BLINK_HALF_MS);
        GPIOA->BSRR = (1UL << (LD2_PIN + 16U));     /* reset: LED off */
        delay_ms(BLINK_HALF_MS);
    }
}
