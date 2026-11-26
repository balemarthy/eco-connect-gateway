/*
 * lwIP platform hooks: assert and random numbers.
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "arch/cc.h"
#include "stm32f4xx_hal.h"
#include "uart_console.h"

void lwip_port_assert(const char *msg, const char *file, int line)
{
    (void)line;
    console_panic("lwip assert: ");
    console_panic(msg);
    console_panic(" @ ");
    console_panic(file);
    console_panic("\r\n");
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

/* xorshift32, seeded from the device UID and the cycle of the first call.
   Good enough for TCP initial sequence numbers, DHCP xid and port selection;
   not for cryptography. */
uint32_t lwip_port_rand(void)
{
    static uint32_t s;

    if (s == 0U) {
        s = HAL_GetUIDw0() ^ (HAL_GetUIDw1() << 7) ^ (HAL_GetUIDw2() << 13) ^ SysTick->VAL ^ HAL_GetTick();
        if (s == 0U) {
            s = 0x2545F491U;
        }
    }
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
