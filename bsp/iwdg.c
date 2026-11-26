/*
 * IWDG on the registers (RM0383 section 17).
 *
 *   t = (RLR + 1) * prescaler / f_LSI,  prescaler /64 -> 2 ms per count at 32 kHz
 * The LSI varies from about 17 to 47 kHz over parts and temperature, so the real
 * timeout can be 0.7x .. 1.9x the nominal one. Feed well inside it.
 */
#include "stm32f4xx.h"

#include "iwdg.h"

#define IWDG_KEY_ENABLE   0xCCCCU
#define IWDG_KEY_RELOAD   0xAAAAU
#define IWDG_KEY_ACCESS   0x5555U
#define LSI_NOMINAL_HZ    32000U
#define IWDG_PRESCALER    64U
#define IWDG_PR_DIV64     4U

void iwdg_start(uint32_t timeout_ms)
{
    uint32_t reload = (timeout_ms * (LSI_NOMINAL_HZ / IWDG_PRESCALER)) / 1000U;
    if (reload > 0x0FFFU) {
        reload = 0x0FFFU;
    }
    if (reload == 0U) {
        reload = 1U;
    }

    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;   /* no resets while halted in the debugger */

    IWDG->KR = IWDG_KEY_ENABLE;                        /* starts the LSI and the counter */
    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR = IWDG_PR_DIV64;
    IWDG->RLR = reload - 1U;
    while (IWDG->SR != 0U) {                           /* PVU/RVU: registers updated */
    }
    IWDG->KR = IWDG_KEY_RELOAD;
}

void iwdg_feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

bool iwdg_caused_reset(void)
{
    bool wd = (RCC->CSR & RCC_CSR_IWDGRSTF) != 0U;
    RCC->CSR |= RCC_CSR_RMVF;
    return wd;
}
