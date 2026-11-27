/*
 * Eco-Connect gateway - entry point.
 * Hardware init, then create the tasks and hand over to the scheduler.
 *
 * Rule for this function: from the first FreeRTOS call (log_init creates a queue)
 * until vTaskStartScheduler(), the kernel keeps BASEPRI raised, so interrupts at
 * priority 5..15 - including the HAL tick on TIM11 - do not run. HAL_Delay() and
 * HAL_GetTick() timeouts must not be used here. Chip bring-up that needs delays
 * (ENC28J60 in the tcpip thread, W25Q in the storage task) runs after the start.
 */
#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_MAIN
#include "log.h"

#include "app_task.h"
#include "board.h"
#include "clock.h"
#include "commands.h"
#include "http_server.h"
#include "iwdg.h"
#include "net_init.h"
#include "proto_task.h"
#include "sensor.h"
#include "io.h"
#include "io_board.h"
#include "storage_task.h"
#include "supervisor.h"
#include "task_heartbeat.h"
#if defined(APP_DEMO) && APP_DEMO
#include "demo.h"                                 /* make DEMO=...: one extra demo task */
#endif

int main(void)
{
    HAL_Init();                                   /* HAL tick on TIM11, NVIC priority group 4 */
    clock_source_t src = clock_init();
    board_init();
    bool wd_reset = iwdg_caused_reset();
    io_handle_t con = io_open(IO_BOARD_CONSOLE_PATH, 0U);   /* polled until the scheduler runs */
    log_init();

    static const char banner[] = "\r\nEco-Connect boot\r\n";
    (void)io_write(con, banner, sizeof banner - 1U);
    LOG_I("boot");
    if (wd_reset) {
        LOG_W("last reset by the watchdog");
    }
    LOG_I("SYSCLK=%lu src=%s", (unsigned long)HAL_RCC_GetSysClockFreq(), clock_source_name(src));
    LOG_I("FreeRTOS %s", tskKERNEL_VERSION_NUMBER);

    sup_start();                                  /* state machine + task watchdog, first */
    commands_init();                              /* GET-SET registry table */
    if (io_open("/SPI2/", 0U) == NULL) {          /* bus for the ENC28J60 and the W25Q */
        LOG_E("/SPI2/ open failed");
    }
    net_init();                                   /* tcpip thread adds the ENC28J60 netif */
    sensor_start();                               /* sampling timer -> readings queue */
    app_start();                                  /* readings queue -> latest reading */
    storage_start();                              /* W25Q probe, JSON lines, RAM tail for /log */
    proto_start();                                /* framed serial link on USART1 */
    http_server_start();                          /* GET /temp, POST /interval, ... on :80 */
    heartbeat_start();
#if defined(APP_DEMO) && APP_DEMO
    demo_start();                                 /* tcp_echo | mqtt | plusio | ota */
#endif
    vTaskStartScheduler();

    /* Only reached if the idle or timer task could not be created. */
    io_panic_puts("scheduler failed\r\n");
    for (;;) {
    }
}
