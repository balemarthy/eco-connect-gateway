/*
 * Eco-Connect gateway - entry point.
 * Hardware init, then create the tasks and hand over to the scheduler.
 */
#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_MAIN
#include "log.h"

#include "board.h"
#include "clock.h"
#include "spi_bus.h"
#include "task_heartbeat.h"
#include "uart_console.h"

int main(void)
{
    HAL_Init();                                   /* HAL tick on TIM11, NVIC priority group 4 */
    clock_source_t src = clock_init();
    board_init();
    (void)console_init();
    log_init();

    console_puts("\r\nEco-Connect boot\r\n");
    LOG_I("boot");
    LOG_I("SYSCLK=%lu src=%s", (unsigned long)HAL_RCC_GetSysClockFreq(), clock_source_name(src));
    LOG_I("FreeRTOS %s", tskKERNEL_VERSION_NUMBER);

    (void)spi_bus_init();

    log_set_level(LOG_MOD_HB, LOG_LVL_DEBUG);     /* B1 toggles it back to info */
    heartbeat_start();
    vTaskStartScheduler();

    /* Only reached if the idle or timer task could not be created. */
    console_panic("scheduler failed\r\n");
    for (;;) {
    }
}
