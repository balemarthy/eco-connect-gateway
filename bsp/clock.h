#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

typedef enum {
    CLOCK_SRC_HSE_BYPASS = 0,   /* 8 MHz MCO from the ST-LINK (normal case) */
    CLOCK_SRC_HSI_FALLBACK,     /* HSE did not start: 16 MHz HSI feeds the PLL */
    CLOCK_SRC_FAILED            /* PLL or bus setup failed: still on reset clock */
} clock_source_t;

/* 100 MHz SYSCLK/HCLK, APB1 50 MHz, APB2 100 MHz, 3 flash wait states. */
clock_source_t clock_init(void);
const char *clock_source_name(clock_source_t src);

#endif /* CLOCK_H */
