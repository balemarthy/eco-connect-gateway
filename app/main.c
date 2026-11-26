/*
 * Step 03: FreeRTOS V11.1.0 with a heartbeat task.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "clock.h"
#include "task_heartbeat.h"
#include "uart_console.h"

int main(void)
{
    HAL_Init();                                   /* HAL tick on TIM11, NVIC priority group 4 */
    clock_source_t src = clock_init();
    board_init();
    (void)console_init();

    console_puts("\r\nEco-Connect boot\r\n");
    console_printf("SYSCLK=%lu src=%s\r\n", (unsigned long)HAL_RCC_GetSysClockFreq(), clock_source_name(src));
    console_puts("FreeRTOS " tskKERNEL_VERSION_NUMBER "\r\n");

    heartbeat_start();
    vTaskStartScheduler();

    /* Only reached if the idle or timer task could not be created. */
    console_panic("scheduler failed\r\n");
    for (;;) {
    }
}
