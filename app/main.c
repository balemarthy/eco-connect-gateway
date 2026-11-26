/*
 * Eco-Connect gateway - entry point.
 * Hardware init, then create the tasks and hand over to the scheduler.
 *
 * Step 07: a packet monitor task exercises the ENC28J60 RX/TX path without a
 * TCP/IP stack: it counts received frames and sends one broadcast test frame
 * (EtherType 88B5h, reserved for local experiments) every 2 s.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_MAIN
#include "log.h"

#include "board.h"
#include "clock.h"
#include "enc28j60.h"
#include "net_init.h"
#include "spi_bus.h"
#include "task_heartbeat.h"
#include "uart_console.h"

#define MON_STACK_WORDS   512U
#define MON_PRIORITY      (tskIDLE_PRIORITY + 3U)
#define MON_TX_PERIOD_MS  2000U
#define MON_LOG_PERIOD_MS 5000U

static StaticTask_t mon_tcb;
static StackType_t  mon_stack[MON_STACK_WORDS];
static uint8_t      mon_frame[ENC_MAX_FRAME];
static uint8_t      mac[6];

static uint16_t build_test_frame(uint8_t *f)
{
    static const char payload[] = "eco-connect enc28j60 test";
    memset(f, 0xFF, 6);                   /* broadcast destination */
    memcpy(&f[6], mac, 6);
    f[12] = 0x88U;
    f[13] = 0xB5U;
    memcpy(&f[14], payload, sizeof payload);
    return (uint16_t)(14U + sizeof payload);
}

static void monitor_task(void *arg)
{
    (void)arg;
    TickType_t last_tx  = xTaskGetTickCount();
    TickType_t last_log = last_tx;
    bool link = enc28j60_link_up();

    (void)enc28j60_start(xTaskGetCurrentTaskHandle());
    LOG_I("link %s", link ? "up" : "down");

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));   /* INT, or poll every 100 ms */

        uint32_t ev = enc28j60_service();
        if ((ev & ENC_EVT_LINK) != 0U) {
            link = enc28j60_link_up();
            LOG_I("link %s", link ? "up" : "down");
        }
        while (enc28j60_receive(mon_frame, sizeof mon_frame) > 0U) {
        }

        TickType_t now = xTaskGetTickCount();
        if (link && (now - last_tx) >= pdMS_TO_TICKS(MON_TX_PERIOD_MS)) {
            last_tx = now;
            uint16_t n = build_test_frame(mon_frame);
            if (enc28j60_send(mon_frame, n) != ENC_OK) {
                LOG_W("tx failed");
            }
        }
        if ((now - last_log) >= pdMS_TO_TICKS(MON_LOG_PERIOD_MS)) {
            last_log = now;
            enc_stats_t s;
            enc28j60_get_stats(&s);
            LOG_I("rx frames=%lu tx=%lu drop=%lu ovf=%lu txerr=%lu",
                  (unsigned long)s.rx_frames, (unsigned long)s.tx_frames, (unsigned long)s.rx_dropped,
                  (unsigned long)s.rx_overflows, (unsigned long)s.tx_errors);
        }
    }
}

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

    board_mac_address(mac);
    if (enc28j60_init(mac) == ENC_OK) {
        TaskHandle_t h = xTaskCreateStatic(monitor_task, "enc", MON_STACK_WORDS, NULL, MON_PRIORITY,
                                           mon_stack, &mon_tcb);
        configASSERT(h != NULL);
    }

    net_init();
    heartbeat_start();
    vTaskStartScheduler();

    /* Only reached if the idle or timer task could not be created. */
    console_panic("scheduler failed\r\n");
    for (;;) {
    }
}
