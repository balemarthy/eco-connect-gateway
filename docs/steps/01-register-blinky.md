# Step 01 - Toolchain and register blinky

## What we are solving
Get from C source to a flashable image with nothing hidden: our own Makefile, our own linker
script, the vendor startup file, and a blinky that touches registers directly.

## Why
Every later layer (HAL, RTOS, network) sits on these pieces. If you can explain how the reset
vector reaches `main()`, where `.data` is copied from, and what `--gc-sections` throws away,
debugging a hard fault later is much easier.

## What changed
- `Makefile` - compile, assemble, link, objcopy to `.hex`/`.bin`, print the size. Sources are
  listed per folder; `-MMD -MP` keeps header dependencies.
- `build.ps1` - adds the STM32CubeIDE 2.2.0 toolchain (gcc 14.3) and make to PATH for one call.
- `bsp/STM32F411RETX_FLASH.ld` - memory map (512 K flash, 128 K RAM), section placement, the
  symbols the startup code needs (`_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss`, `_estack`).
- `bsp/startup_stm32f411retx.s` - ST vector table and `Reset_Handler` (copy `.data`, zero `.bss`,
  `SystemInit`, `__libc_init_array`, `main`).
- `bsp/syscalls.c` - newlib stubs so the link does not pull the "not implemented" stubs.
- `third_party/cmsis/` - CMSIS-Core and the STM32F411xE device header (register names).
- `app/main.c` - `SystemInit` enables the FPU; SysTick is a polled 1 ms base; LD2 (PA5) toggles.

## How to verify
```
.\build.ps1 clean all
```
Expected tail:
```
   text    data     bss     dec     hex filename
    ...                                 build/eco-connect-gateway.elf
```
`build/` holds `.elf`, `.hex`, `.bin`, `.map`. On the board LD2 blinks at about 1 Hz.

Mentee checks: find `Reset_Handler` and `_sidata` in the `.map` file; change `BLINK_HALF_MS`
and confirm the rate with a phone stopwatch.
