/*
 * Clock tree for 100 MHz.
 *
 *   HSE 8 MHz (bypass, MCO of the ST-LINK) / M 4 = 2 MHz VCO input
 *   VCO = 2 MHz * N 100 = 200 MHz
 *   SYSCLK = VCO / P 2 = 100 MHz     (PLLQ = 4 -> 50 MHz, USB is not used)
 *   AHB /1 = 100 MHz, APB1 /2 = 50 MHz (max 50), APB2 /1 = 100 MHz
 *   Flash: 3 wait states at 2.7-3.6 V and 90-100 MHz (RM0383 table 6), voltage scale 1.
 *
 * If HSE does not start (solder bridges changed, or no MCO) the same PLL is fed
 * from HSI 16 MHz with M = 8, so the rest of the firmware sees the same clocks.
 */
#include "clock.h"
#include "stm32f4xx_hal.h"

static HAL_StatusTypeDef pll_start(uint32_t source, uint32_t pllm)
{
    RCC_OscInitTypeDef osc = {0};

    if (source == RCC_PLLSOURCE_HSE) {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        osc.HSEState       = RCC_HSE_BYPASS;
    } else {
        osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState            = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    }
    osc.PLL.PLLState  = RCC_PLL_ON;
    osc.PLL.PLLSource = source;
    osc.PLL.PLLM      = pllm;
    osc.PLL.PLLN      = 100U;
    osc.PLL.PLLP      = RCC_PLLP_DIV2;
    osc.PLL.PLLQ      = 4U;
    return HAL_RCC_OscConfig(&osc);
}

clock_source_t clock_init(void)
{
    clock_source_t src = CLOCK_SRC_HSE_BYPASS;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    if (pll_start(RCC_PLLSOURCE_HSE, 4U) != HAL_OK) {
        src = CLOCK_SRC_HSI_FALLBACK;
        if (pll_start(RCC_PLLSOURCE_HSI, 8U) != HAL_OK) {
            return CLOCK_SRC_FAILED;
        }
    }

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) {
        return CLOCK_SRC_FAILED;
    }
    return src;
}

const char *clock_source_name(clock_source_t src)
{
    switch (src) {
    case CLOCK_SRC_HSE_BYPASS:   return "HSE";
    case CLOCK_SRC_HSI_FALLBACK: return "HSI";
    default:                     return "FAIL";
    }
}
