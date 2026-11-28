# IO layer (`app/io/`)

Every UART, SPI and I2C peripheral of the gateway is reached through one small API, modelled
on the FreeRTOS+IO API style: a task opens a peripheral by name and then uses `io_read`,
`io_write` and `io_ioctl` on the handle. What the board offers, on which pins, with which
interrupt priority, default speed and transfer mode, is one table in one header. Static memory
only: no `malloc`, every kernel object is created static.

```
 app/ and drivers/      log.c  proto_task.c  enc28j60.c  w25q.c  sensor_task.c  plusio_demo.c
                           \        |            |          |          |             /
 app/io/io.h               io_open("/UART2/") / io_read / io_write / io_ioctl
                                               |
 app/io/io_core.c          path lookup, request checks, mode rules, statistics
                           |            |               |                    |
 transfer modes            io_mode_polled.c  io_mode_circ_buf.c  io_mode_char_queue.c  io_mode_zero_copy.c
                                               |
 io_driver_t               io_uart.c         io_spi.c          io_i2c.c
                                               |
 board table               io_board_f411.h (+ io_board.c builds it)
                                               |
 STM32F4 HAL / registers   HAL_*_Init for set-up, register access on the data path (UART, SPI)
```

## API (`io.h`)

| Call | Does |
|---|---|
| `io_handle_t io_open(const char *path, uint32_t flags)` | `path` exactly as in the board table (`"/UART1/"`); `flags` must be 0. First open initialises the hardware and applies the board defaults; every later open returns the same handle. `NULL`: unknown path, bad flags, or the hardware/default mode failed. |
| `size_t io_write(h, buf, len)` | Bytes accepted. Polled: sent. Character queue: queued. UART zero copy: transfer started (the buffer belongs to the driver until the write mutex comes back). |
| `size_t io_read(h, buf, len)` | Bytes delivered. Polled and character queue: waits for `len` bytes or the RX timeout. Circular buffer: returns as soon as at least one byte is there, or 0 after the RX timeout. |
| `io_status_t io_ioctl(h, request, value)` | Mode selection, mutex, timeouts, speed, SPI/I2C specifics, status. Integer values as `IO_VAL(x)` (= `(void *)(uintptr_t)x`), structs by pointer. |
| `io_status_str()`, `io_path()` | Text for logs. |
| `io_panic_puts(s)` | Fault paths only: busy-waits the string out of the board console, IO layer and interrupts bypassed. |

A short count from `io_read`/`io_write` is explained by `IO_IOCTL_GET_LAST_STATUS`. Not callable
from an ISR. One control block per peripheral, shared by every task that opens it: sharing is
what the zero-copy write mutex is for.

Status codes: `IO_OK`, `IO_ERR_PARAM` (NULL, value out of range), `IO_ERR_UNSUPPORTED` (request
or mode not offered by this peripheral type), `IO_ERR_NO_MEMORY` (more than the board reserved),
`IO_ERR_STATE` (not now: mode change while the mutex is held, transfer without chip select, ...),
`IO_ERR_NOT_OWNER` (zero copy without holding the mutex), `IO_ERR_TIMEOUT`, `IO_ERR_IO`
(NACK, bus error), `IO_ERR_NOT_READY` (not opened / init failed).

## Files

| File | Role | Host tested |
|---|---|---|
| `io.h` | public API, status codes, ioctl requests, `io_spi_xfer_t`, `io_i2c_xfer_t`, `io_stats_t` | - |
| `io_driver.h` | `io_driver_t`, board descriptor `io_desc_t`, control block `io_peripheral_t`, `io_board_t` | - |
| `io_core.c` | `io_open/read/write/ioctl`, path rules, request checks, mode rules | yes |
| `io_mode.h`, `io_mode_*.c` | the four transfer modes | yes |
| `io_stm32f4.h` | STM32F4 hardware structs read from the table (`io_uart_hw_t`, `io_spi_hw_t`, `io_i2c_hw_t`) | - |
| `io_uart.c`, `io_spi.c`, `io_i2c.c` | drivers for USART1/2/6, SPI1-5, I2C1-3 of the F411 | compiled only |
| `io_board.h` | selects the board table for the build (`STM32F411xE` -> `io_board_f411.h`) | - |
| `io_board_f411.h` | NUCLEO-F411RE descriptor table | - |
| `io_board.c` | expands the table into descriptors, control blocks, driver state, buffers | - |

## Board descriptor table (`io_board_f411.h`)

One X-macro line per peripheral; `io_board.c` expands it into a `const io_desc_t` (path, type,
driver, hardware struct, driver state, default modes, default lengths and timeouts, static buffers
and mode objects, chip-select count) and a zeroed `io_peripheral_t`. A buffered mode is available
only where the line reserves memory for it; elsewhere its arrays are zero-length and cost nothing.

| Path | Peripheral, pins | IRQ prio | Default speed | TX mode at open | RX mode at open | Timeouts TX / RX | Reserve TX / RX |
|---|---|---|---|---|---|---|---|
| `/UART1/` | USART1, PA9 TX, PA10 RX | 5 | 115200 8N1 | polled | circular buffer 512 | 100 / 1000 ms | 0 / 512 B |
| `/UART2/` | USART2 (ST-LINK VCP), PA2 TX, PA3 RX | 7 | 115200 8N1 | character queue 512 | polled | 1000 / 100 ms | 512 / 64 B |
| `/SPI2/` | SPI2, PB13 SCK, PB14 MISO, PB15 MOSI; CS 0 = ENC28J60 PB12, CS 1 = W25Q PB1 | - (polled) | 12.5 MHz, mode 0 | zero copy (bus mutex) | polled | 50 / 50 ms | 0 / 0 |
| `/I2C1/` | I2C1, PB8 SCL, PB9 SDA (open drain) | - (polled) | 100 kHz | polled | polled | 100 / 100 ms | 0 / 0 |

`IO_BOARD_CONSOLE_PATH` (`"/UART2/"`) is the log console, `IO_BOARD_CONSOLE_UART` the panic output.
Chip-select indices are named (`IO_SPI2_CS_ENC28J60`, `IO_SPI2_CS_W25Q`) so drivers never use a pin.
`bsp/board.h` keeps only what is not an IO peripheral: LED, button, ENC28J60 INT/RESET, EXTI priority.

## Transfer modes

| Mode | Directions / types | Memory | Who waits | Interrupt does |
|---|---|---|---|---|
| polled | TX and RX, all types | none | the calling task busy-waits each flag with the TX/RX timeout (`io_polled_wait`) | nothing |
| circular buffer | RX, UART | ring of 16..reserve bytes, power of two; binary semaphore | `io_read` sleeps on the semaphore until data or the RX timeout | stores each byte, gives the semaphore; full ring -> byte dropped, `rx_overruns` + 1 |
| character queue | TX and RX, UART | FreeRTOS queue of 1-byte items, length <= reserve | TX: only when the queue is full (up to the TX timeout); RX: until `len` bytes or the RX timeout | TX: one byte per TXE, TXE interrupt off when empty; RX: one byte per RXNE into the queue |
| zero copy | TX, all types | write mutex | `IO_IOCTL_OBTAIN_WRITE_MUTEX` (timeout) | UART: sends from the caller's buffer, gives the mutex back after the last byte |

Rules (`io_core.c`, host tested):
- `io_write` in zero-copy mode needs the caller to hold the write mutex, else `IO_ERR_NOT_OWNER`.
  SPI and I2C also check it for reads, chip selects, transfers and settings.
- The mutex is not recursive (`IO_ERR_STATE` on a second obtain). SPI/I2C use a FreeRTOS mutex
  (priority inheritance, released by the holder). UART zero copy uses a binary semaphore that the
  driver passes to the TX interrupt; `IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE` waits until it is back.
- A mode change switches that direction's interrupt off, rebuilds the buffer, then switches it on
  again; bytes still buffered are dropped. Refused while the mutex is held. A refused selection
  leaves the direction polled.
- A character queue keeps the length of its first selection (another length: `IO_ERR_STATE`).
- Before the scheduler runs (the kernel keeps the UART interrupts masked until then) every UART
  write is polled and the mutex calls succeed without a kernel object. The boot banner and
  early log lines use this.

## ioctl requests

| Request | Value | Types | Notes |
|---|---|---|---|
| `IO_IOCTL_USE_POLLED_TX` / `_RX` | - | all | |
| `IO_IOCTL_USE_ZERO_COPY_TX` | - | all | creates the write mutex once |
| `IO_IOCTL_USE_CHAR_QUEUE_TX` / `_RX` | queue length | UART | 1..reserve, else `PARAM` / `NO_MEMORY` |
| `IO_IOCTL_USE_CIRCULAR_BUFFER_RX` | ring size | UART | power of two >= 16, <= reserve |
| `IO_IOCTL_OBTAIN_WRITE_MUTEX` | timeout ms | all (zero-copy mode) | `TIMEOUT`, `STATE` if not zero copy or already held by the caller |
| `IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE` | timeout ms | all | obtain + give back; `OK` at once outside zero copy |
| `IO_IOCTL_RELEASE_WRITE_MUTEX` | - | all (zero-copy mode) | holder only; SPI drops a chip select still on |
| `IO_IOCTL_SET_TX_TIMEOUT` / `_RX_TIMEOUT` | ms | all | per handle, default from the table |
| `IO_IOCTL_CLEAR_RX_BUFFER` | - | UART | ring or RX queue emptied |
| `IO_IOCTL_WAIT_TX_EMPTY` | timeout ms | UART | character queue drained / zero-copy write done |
| `IO_IOCTL_SET_SPEED` | baud / Hz | UART 1200-921600, SPI 0.1-50 MHz (next lower prescaler step), I2C 10-400 kHz | UART: TX must be idle; SPI: no chip selected |
| `IO_IOCTL_SET_INTERRUPT_PRIORITY` | 5..15 | UART | 5 = `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (the ISRs call FromISR functions); SPI/I2C have no IRQ (`UNSUPPORTED`) |
| `IO_IOCTL_SPI_SET_MODE` | 0..3 | SPI | CPOL << 1 \| CPHA |
| `IO_IOCTL_SPI_SELECT` | chip-select index | SPI | < chip-select count; `STATE` if another chip is selected |
| `IO_IOCTL_SPI_DESELECT` | - | SPI | waits until BSY clears, then CS high |
| `IO_IOCTL_SPI_TRANSFER` | `io_spi_xfer_t *` | SPI | full duplex inside the current select; `tx` NULL sends 0xFF, `rx` NULL discards |
| `IO_IOCTL_I2C_SET_SLAVE_ADDRESS` | 0x08..0x77 | I2C | 7-bit, the driver shifts it |
| `IO_IOCTL_I2C_WRITE_READ` | `io_i2c_xfer_t *` | I2C | 1- or 2-byte register address, repeated START, `rd_len` bytes |
| `IO_IOCTL_GET_LAST_STATUS` | `io_status_t *` | all | result of the last `io_read`/`io_write` |
| `IO_IOCTL_GET_STATS` | `io_stats_t *` | all | bytes TX/RX, RX overruns, RX errors (UART FE/NE, I2C NACK/bus), timeouts |

`io_ioctl()` checks every request against the descriptor (`io_ioctl_check`) before anything runs,
so a wrong value never reaches a driver.

## How each user uses it

| User | Path | How |
|---|---|---|
| boot banner, `main.c` | `/UART2/` | `io_open` before `log_init()`; the write is polled (no scheduler yet) |
| log task, `app/log/log.c` | `/UART2/` | before the scheduler: lines written polled; afterwards the log task is the only writer and `io_write` copies each line into the 512-character TX queue (same behaviour as the earlier 512-byte ring: the writer goes on, the TXE interrupt sends; it waits only when the queue is full, now at most the 1000 ms TX timeout) |
| fault hooks (`rtos_hooks.c`, `port_misc.c`) | console USART | `io_panic_puts()` |
| protocol task, `app/proto/proto_task.c` | `/UART1/` | RX circular buffer (512 B, filled by the RXNE interrupt, overruns counted); `io_read(link, chunk, 32)` returns on the first bytes or after 1000 ms (supervisor check-in); TX polled with 100 ms timeout, one writer |
| ENC28J60, `drivers/enc28j60/enc28j60.c` | `/SPI2/` | each public call: obtain the bus mutex (50 ms), then per SPI cycle `SPI_SELECT(IO_SPI2_CS_ENC28J60)`, `SPI_TRANSFER` (register ops, full duplex) or `io_write`/`io_read` (buffer memory), `SPI_DESELECT`; release at the end. `rx_begin` ... `rx_end` keeps the mutex across the whole frame, as before |
| W25Q, `drivers/w25q/w25q.c` | `/SPI2/` | one command = obtain, `SPI_SELECT(IO_SPI2_CS_W25Q)`, `io_write(cmd)`, `io_read(data)`, deselect, release (`cmd()`); the mentee data-path functions follow the same pattern |
| sensor, `app/sensor/sensor_task.c` (`make SENSOR=i2c`) | `/I2C1/` | opens the bus in `init`; the sensor itself (address, probe, `I2C_WRITE_READ` of the value) is the mentee task |
| `make DEMO=plusio`, `app/demos/plusio_demo.c` | all four | see `docs/demos/plusio.md` |

### Shared SPI bus, step by step
```
io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(50));      // or IO_ERR_TIMEOUT: give up, retry later
io_ioctl(spi, IO_IOCTL_SPI_SELECT, IO_VAL(IO_SPI2_CS_W25Q)); // CS low
io_write(spi, cmd, 4);                                       // 03h + 24-bit address
io_read(spi, buf, n);                                        // clocks out 0xFF, keeps MISO
io_ioctl(spi, IO_IOCTL_SPI_DESELECT, NULL);                  // last bit out, CS high
io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL);
```
Several chip-select cycles may sit under one obtain/release (the ENC28J60 does). A chip select is
only accepted from the mutex holder, and one chip at a time, so the ENC28J60 (tcpip thread) and the
W25Q (storage task) can never drive the bus together.

## Adding things

**A peripheral on this board**: add one `X(...)` line to `IO_BOARD_DEVICES` in `io_board_f411.h`
with its `IO_BOARD_<ID>_HW` initialiser (instance, IRQ and priority, pins, AF, default speed),
default modes, timeouts and reserves. USART1/2/6, SPI1-5 and I2C1-3 have drivers already.
A device on an existing SPI bus is one more entry in `IO_BOARD_SPI2_CS_PINS` and the enum.

**Another board**: copy `io_board_f411.h` to `io_board_<board>.h`, change the pins, IRQs and
table, and select it in `io_board.h` on the board's define. The drivers take everything from the
table, so they do not change for another STM32F4 board. Another MCU family needs its own
`io_<family>.h` hardware structs and the three drivers (`io_driver_t`: `open`, `write`, `read`,
`ioctl`, `mode_changed`); `io_core.c` and the mode modules stay as they are.

**A peripheral type** (e.g. a timer capture or CAN): a value in `io_type_t`, the request checks in
`io_ioctl_check`, the modes it may use in `io_mode_allowed`, and a driver.

## Host tests
`tests/test_io.c` (394 checks) runs `io_core.c` and the four mode modules on the host with a test
board table and fake drivers; `tests/stubs/` stands in for the FreeRTOS calls (single thread, a test
tick, a switchable "current task", and a hook that plays the ISR or another task while a call would
block). Covered: path rules and lookup, open once per peripheral, failing driver and bad board
default, read/write dispatch and statistics, every ioctl validation rule per type, mode selection
and reserve limits, the bus mutex across two tasks and before the scheduler, the ISR-released UART
mutex, the circular buffer (full, wrap of the 32-bit indices, wake-up by the ISR, timeout, clear),
the character queue (kick before blocking, drain, short count on timeout, RX count/timeout) and the
polled deadline (also across a clock wrap). The drivers themselves are compiled only.

## Cost
`DEMO=none` with the IO layer: 98,572 B flash / 90,216 B RAM, against 91,328 / 89,056 before it
(+7,244 B flash, +1,160 B RAM). Flash: the IO layer itself about 7.1 KB (core 1.7 KB, modes 1.1 KB,
UART 1.4 KB, SPI 1.4 KB, I2C 1.1 KB, table 0.5 KB) plus 2.9 KB of HAL I2C now that I2C1 has a real
driver, minus about 2.8 KB for the four drivers it replaces. RAM: four control blocks with their write mutex, the per-entry HAL handles,
the 512 B console queue and 512 B link ring (the same sizes as the ring and stream buffer before),
and 64 B for console RX.

## History
The IO layer arrives in `89b9dc5` (`app/io/`, host tests) and `5ae0f5f` (every user switched onto it).
`89b9dc5` already removes the old bus drivers and `app/plusio/`, so the firmware does not build at that
commit while the host tests pass; it builds again at `5ae0f5f`.

## Limits
- SPI and I2C transfers are polled (no DMA, no interrupt-driven SPI/I2C modes); UART zero copy is
  implemented and host tested but no default user selects it.
- Character queue costs one queue operation per byte; fine at 115200 baud, not for fast links.
- Compiled only: nothing here has run on the board yet (console, link, ENC28J60, W25Q, I2C NACK and
  recovery paths are unverified on hardware).
