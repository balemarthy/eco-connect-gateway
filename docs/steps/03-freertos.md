# Step 03 - FreeRTOS V11.1.0 with a heartbeat task

## What we are solving
Move from a super-loop to tasks. The kernel needs SysTick, PendSV and SVC; the HAL still
needs its own 1 ms tick for timeouts.

## Why
- The gateway has several jobs with different timing (sampling, network, console, storage).
  Tasks plus queues keep them apart.
- SysTick goes to the kernel. If the HAL kept SysTick, `HAL_Delay()` and every HAL timeout
  would depend on the scheduler running. TIM11 gives the HAL an independent tick.
- Hooks turn silent failures (stack overflow, heap exhausted, assert) into one console line.

## What changed
- `third_party/freertos/` - kernel V11.1.0 (MIT, `LICENSE.md`): core files, `include/`,
  `portable/GCC/ARM_CM4F`, `portable/MemMang/heap_4.c`.
- `app/FreeRTOSConfig.h` - 1 kHz tick, 7 priorities, static + dynamic allocation, stack check
  method 2, max syscall interrupt priority 5, port handlers mapped to `SVC_Handler`,
  `PendSV_Handler`, `SysTick_Handler`.
- `bsp/hal_timebase_tim.c` - `HAL_InitTick()` on TIM11 (1 MHz count, 1 ms update interrupt).
- `bsp/stm32f4xx_it.c` - SysTick handler removed (the port provides it).
- `app/rtos_hooks.c` - stack overflow, malloc failed and assert hooks (polled print, halt).
- `drivers/uart_console.c` - `console_panic()`: register-level print usable with IRQs off.
- `app/task_heartbeat.c/.h` - static task, toggles LD2 with `xTaskDelayUntil`.
- `app/main.c` - creates the heartbeat task and starts the scheduler.

## How to verify
```
.\build.ps1 clean all
```
Console after reset:
```
Eco-Connect boot
SYSCLK=100000000 src=HSE
FreeRTOS V11.1.0
scheduler started
```
LD2 toggles every 500 ms, now from the task.

Mentee checks: set `HB_STACK_WORDS` to 32 and confirm the `stack overflow: hb` line; put a
breakpoint in `TIM1_TRG_COM_TIM11_IRQHandler` and in `SysTick_Handler` and see each one fire.
