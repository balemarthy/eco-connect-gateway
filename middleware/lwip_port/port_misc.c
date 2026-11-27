/*
 * lwIP platform hooks: assert and random numbers.
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "arch/cc.h"
#include "stm32f4xx_hal.h"
#include "io.h"

void lwip_port_assert(const char *msg, const char *file, int line)
{
    (void)line;
    io_panic_puts("lwip assert: ");
    io_panic_puts(msg);
    io_panic_puts(" @ ");
    io_panic_puts(file);
    io_panic_puts("\r\n");
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
