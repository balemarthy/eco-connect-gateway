/*
 * Step 02: 100 MHz clock tree, HAL and a polled UART console.
 */
#include "board.h"
#include "clock.h"
#include "uart_console.h"

int main(void)
{
    HAL_Init();                                   /* flash prefetch/caches, SysTick 1 ms, NVIC grouping */
    clock_source_t src = clock_init();
    board_init();
    (void)console_init();

    console_puts("\r\nEco-Connect boot\r\n");
    console_printf("SYSCLK=%lu\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    console_printf("HCLK=%lu PCLK1=%lu PCLK2=%lu src=%s\r\n",
                   (unsigned long)HAL_RCC_GetHCLKFreq(),
                   (unsigned long)HAL_RCC_GetPCLK1Freq(),
                   (unsigned long)HAL_RCC_GetPCLK2Freq(),
                   clock_source_name(src));

    for (;;) {
        board_led_toggle();
        HAL_Delay(500U);
    }
}
