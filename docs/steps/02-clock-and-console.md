# Step 02 - 100 MHz clock tree, HAL and UART console

## What we are solving
The core runs at 16 MHz HSI after reset and we have no way to print. We set the clock tree to
100 MHz and bring up a console on the ST-LINK virtual COM port.

## Why
- Every peripheral clock (SPI2 for the ENC28J60, USART baud rates, timer ticks) is derived
  from this tree. Fix it once, document it, and later drivers just read `HAL_RCC_GetPCLKxFreq()`.
- A console is the first observability tool. Later it becomes the log output.

## What changed
- `third_party/stm32f4xx_hal/` - STM32F4 HAL 1.8.1 (BSD-3-Clause, `LICENSE.md`). The modules
  SPI, I2C and IWDG are included now (from the same release) and compiled when first used.
- `bsp/system_stm32f4xx.c` - ST `SystemInit` (FPU on) and `SystemCoreClock`.
- `bsp/stm32f4xx_hal_conf.h` - which HAL modules are on, `HSE_VALUE` = 8 MHz.
- `bsp/clock.c/.h` - HSE bypass 8 MHz -> PLL (M 4, N 100, P 2) -> 100 MHz; APB1 50 MHz,
  APB2 100 MHz, 3 wait states. Falls back to HSI through the same PLL if HSE does not start.
- `bsp/board.c/.h` - pin map, LED/button, `HAL_UART_MspInit` (USART2 on PA2/PA3, AF7).
- `bsp/stm32f4xx_it.c/.h` - fault handlers, SysTick drives the HAL 1 ms tick (moved in step 03).
- `drivers/uart_console.c/.h` - polled USART2, 115200 8N1.
- `app/main.c` - prints the clock tree, toggles LD2 every 500 ms.

## How to verify
```
.\build.ps1 clean all
```
Open the ST-LINK COM port at 115200 8N1 and press reset:
```
Eco-Connect boot
SYSCLK=100000000
HCLK=100000000 PCLK1=50000000 PCLK2=100000000 src=HSE
```
Mentee check: with a logic analyser on PA2, measure one bit time (8.68 us at 115200).
