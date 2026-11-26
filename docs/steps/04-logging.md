# Step 04 - Per-module log levels over a log task

## What we are solving
Several tasks will print. Polled printing from many tasks garbles lines and stalls the
caller for the whole transmit time (about 87 us per byte at 115200).

## Why
- A log call should cost a format and a queue copy, never a wait on the UART.
- Each module gets its own level so you can turn on DEBUG for one driver without flooding
  the console. The same idea grows into the event log of Project 2.
- If logging cannot keep up, lines are dropped and counted, not queued forever.

## What changed
- `app/log/log.c/.h` - `LOG_E/W/I/D` macros, `LOG_TAG` per file, module table
  (`LOG_MODULES` X-macro), levels per module at run time, static queue of 16 lines,
  log task at low priority, `log_dropped()` counter. Before the scheduler starts, lines go
  straight to the console.
- `drivers/uart_console.c` - after the scheduler starts, TX is interrupt driven: 512-byte
  ring buffer, TXE interrupt, the writer sleeps on a task notification when the ring is full.
- `bsp/board.h` - NVIC priority for the console (7, below the FreeRTOS syscall limit 5).
- `app/task_heartbeat.c` - logs `tick n` at DEBUG every 5 s; B1 toggles the `hb` level.
- `app/main.c` - boot lines through the log module.

## How to verify
```
.\build.ps1 clean all
```
Console:
```
Eco-Connect boot
[I][main] boot
[I][main] SYSCLK=100000000 src=HSE
[I][main] FreeRTOS V11.1.0
[I][hb] scheduler started
[D][hb] tick 1
[D][hb] tick 2
```
Press B1: `[I][hb] level info` and the tick lines stop. Press again to bring them back.

Mentee task: add a new module to `LOG_MODULES` and log from it; then call `LOG_I` 50 times
in a tight loop and read `log_dropped()`.
