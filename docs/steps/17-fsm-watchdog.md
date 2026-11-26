# Step 17 - Gateway state machine and task watchdog supervisor

## What we are solving
Brief G6: the gateway has a visible state (IDLE, SAMPLING, CONNECTED, REPORTING, FAULT)
and a watchdog. A watchdog fed from one timer interrupt only proves that interrupt
still runs; we want "every task is alive", and a reset when one is not.

## Why
- **State machine**: events (reading ok/failed, link up/down, a client served, a task
  stalled) update a few conditions; the state is derived from them in priority order.
  No transition is forgotten, and the logic is pure C with 46 host checks.
- **Sensor-pull recovery**: three bad readings in a row -> FAULT; the next good reading
  brings it back. The sensor task re-initialises its source every 5th failure.
- **Supervisor**: each task registers a window (hb 2 s, net 2 s, log/app/sensor/proto
  3 s, http 8 s, storage 30 s) and checks in from its loop; blocking calls got timeouts
  so idle tasks still check in. The lwIP tcpip thread is probed with `tcpip_try_callback`.
  The IWDG (2 s) is fed only while all windows are met. One miss -> log line, FAULT, no
  more feeding -> reset. The supervisor runs at the lowest priority, so CPU starvation
  also ends in a reset.
- Boot reports `last reset by the watchdog` when the IWDG did it.

## Two corrections to earlier steps (found while adding the check-ins)
1. From the first FreeRTOS call until `vTaskStartScheduler()` the kernel keeps BASEPRI
   raised, so the HAL tick (TIM11, priority 15) does not run and `HAL_Delay()` never
   returns. Steps 06-07 (ENC28J60 init in `main`) and 15-16 (W25Q probe in `main`) hang
   there on hardware. Now: the W25Q probe runs in the storage task, the ENC28J60 init has
   been in the tcpip thread since step 09, and the pre-scheduler fallback in both drivers
   is `board_delay_busy_ms()` (DWT cycle counter, no interrupt needed).
2. Since step 11 the `test:` rule came first in the `Makefile`, so a bare `make` ran the
   host tests. `.DEFAULT_GOAL := all` fixes it (`make clean all` was always correct).

## What changed
- `app/core/gateway_fsm.c/.h` - states, events, `gw_fsm_apply()`.
- `app/core/supervisor.c/.h` - event queue, check-in table, tcpip probe, IWDG feeding.
- `bsp/iwdg.c/.h` - IWDG on registers (/64, 2 s), frozen while the debugger halts the
  core, reset-cause check.
- Tasks: check-ins in log, hb, sensor, app, proto, http, storage, enc_rx; events from
  app (readings), net callbacks (link), http/proto (client served).
- `app/task_heartbeat.c` - LD2 blinks at 100 ms in FAULT; B1 held 3 s = watchdog test.
- `app/registry/commands.c` - `status` shows `state`; `log` lists only non-default levels.
- `bsp/board.c/.h` - `board_delay_busy_ms()`. `Makefile` - `WATCHDOG=0`, default goal.
- `tests/test_fsm.c` - 46 checks.

## How to verify
```
make test
.\build.ps1 clean all
```
Console:
```
[I][sup] watchdog 2000 ms, 9 tasks watched
[I][fsm] IDLE -> SAMPLING (reading_ok)
[I][fsm] SAMPLING -> CONNECTED (link_up)
[I][fsm] CONNECTED -> REPORTING (report)        after curl /temp
[I][fsm] REPORTING -> CONNECTED (report_idle)   10 s later
```
Hold B1 for 3 s:
```
[W][hb] watchdog test: hb stops checking in
[E][sup] task hb missed
[I][fsm] CONNECTED -> FAULT (task_stall)
[E][sup] watchdog will reset the board
... reset ...
[W][main] last reset by the watchdog
```
