# Step 10 - Sensor task, sampling timer and readings queue

## What we are solving
Get a value from a sensor to the part of the firmware that will publish it, at a
configurable interval, with no heap and no shared variables between unrelated tasks.

## Why
- The brief asks for "BLE/sensor thread -> network thread via a queue, no heap in steady
  state" (G3). A static FreeRTOS queue of fixed-size `reading_t` copies is exactly that.
- A software timer gives a stable period that can be changed at run time
  (`xTimerChangePeriod`, used by `POST /interval` in step 14). The timer callback runs in
  the timer service task, so it only notifies; the read happens in the sensor task.
- Queue full policy: drop the oldest. A gateway that serves "the current temperature"
  wants the newest sample; the drop counter shows when the consumer falls behind.
- The value is fixed point (0.01 degC in an `int32_t`). No float formatting is linked.

## What changed
- `app/core/readings.h` - `reading_t {seq, node, status, value_centi, timestamp_ms}`.
- `app/sensor/sensor.h`, `app/sensor/sensor_task.c` - source table, simulated source
  (default), I2C source skeleton, timer + task, interval/node id setters, drop counter.
- `drivers/i2c_bus.c/.h` - I2C1 contract (PB8/PB9, 100 kHz). **Mentee task:** the bodies.
- `app/core/app_task.c/.h` - consumes the queue, keeps the latest reading (`app_latest()`),
  logs each one.
- `bsp/board.h` - I2C1 pins. `Makefile` - `make SENSOR=i2c` selects the I2C source.

## How to verify
```
.\build.ps1 clean all
```
Console, every 2 s:
```
[I][sensor] source sim, interval 2000 ms
[I][app] seq=1 node=1 t=23.00C
[I][app] seq=2 node=1 t=23.13C
```
With `SENSOR=i2c` and the stubs in place you get `[W][app] seq=.. sensor error` until the
I2C driver is written.

Mentee tasks:
1. Implement `drivers/i2c_bus.c` and the two `i2c_sensor_*` functions for your sensor.
2. Make the app task slow (add a `vTaskDelay(5000)`) and watch `sensor_dropped()` climb.
