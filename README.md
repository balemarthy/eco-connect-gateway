# eco-connect-gateway

Eco-Connect IoT gateway firmware for the **NUCLEO-F411RE** (STM32F411RE, Cortex-M4F, 100 MHz,
512 KB flash / 128 KB RAM). A sensor reading travels from a sampling task, through a static
queue, into an application task and out over Ethernet (ENC28J60 on SPI, lwIP) as JSON, and over
a framed serial link (USART1). Readings are logged to a W25Q flash with FatFs.

The repository is built one step at a time. Every commit on `main` is one step and carries its
own write-up in `docs/steps/` (index: `docs/steps/README.md`). Check out a step, read its note,
build it, then move on.

## Stack

| Layer | Choice |
|---|---|
| MCU support | CMSIS + STM32F4 HAL 1.8.1 (vendored in `third_party/`) |
| Peripheral IO | own IO layer modelled on the FreeRTOS+IO API style: `io_open("/UART1/")`, read/write/ioctl, board table (`docs/io-layer.md`) |
| RTOS | FreeRTOS-Kernel V11.1.0, GCC/ARM_CM4F port, heap_4 |
| Network | lwIP 2.2.0 (NO_SYS=0, netconn) over an ENC28J60 on SPI2 |
| Storage | FatFs R0.16 on a W25Q SPI NOR flash |
| Build | hand-written `Makefile`, arm-none-eabi-gcc 14.3 (STM32CubeIDE 2.2.0 toolchain) |
| Host tests | any host gcc (`make test`), e.g. MSYS2 ucrt64 on Windows |

## Build

```
.\build.ps1 clean all          # Windows: adds the CubeIDE toolchain to PATH for this call
make clean all                 # toolchain already on PATH
make test                      # host unit tests (JSON, serial protocol, registry, HTTP, FSM, OTA)
```
Options (always with `clean` when you change one): `DHCP=1`, `SENSOR=i2c`, `WATCHDOG=0`.
Output: `build/eco-connect-gateway.elf/.hex/.bin/.map`. Flash with STM32CubeProgrammer or
drag the `.bin` onto the NUCLEO drive.

### Optional demos (`make DEMO=...`, `.\build.ps1 -Demo ...`)
One extra task on top of the gateway, chosen at build time; settings in `app/config/app_config.h`,
notes in `docs/demos/` (index `docs/demos/README.md`). `DEMO=none` (default) builds exactly the
firmware above. Demo output goes to `build/demo-<name>/`.

| DEMO | What |
|---|---|
| `tcp_echo` | echo server on TCP :7 (netconn) |
| `mqtt` | publishes the reading JSON to `eco/<node>/temp` with coreMQTT (`third_party/coreMQTT`, MIT) |
| `plusio` | small demo task of the IO layer API: request checks, character-queue console RX, shared-SPI JEDEC read, I2C register read, link statistics |
| `ota` | image download with HTTP Range into a FatFs file (or `OTA_STORE=ram`), size + CRC32 check (`SET ota ...`, `POST /ota`) |

Extra make variables: `OTA_STORE=ram` (OTA image in a 16 KB RAM buffer instead of the FatFs file),
`EXTRA_DEFS="-DNAME=value"` (override any `#ifndef` setting of `app/config/app_config.h`).

## Interfaces

| Where | What |
|---|---|
| Console USART2 115200 | `[I][module] message` log lines, level per module |
| HTTP :80 (192.168.1.50) | `GET /` status page, `GET /temp`, `GET /interval`, `POST /interval {"interval_ms":5000}`, `GET /status`, `GET /version`, `GET /log`; JSON errors |
| Serial USART1 115200 | `$GET S 04 temp*52`, `$SET S 0D interval 5000*A2`, `$PING N 00 *C3` (see `app/proto/serial_protocol.h`) |
| Registry keys | `temp`, `interval`, `node`, `log`, `status`, `version`, `history` |

## Repository layout

```
app/          application: main, tasks, log, json, protocol, registry, http, storage, core (FSM, supervisor)
bsp/          board support: startup, linker script, clocks, pins, interrupts, IWDG
app/io/       IO layer: open/read/write/ioctl by path, board table, UART/SPI/I2C drivers, transfer modes
drivers/      device drivers on the IO layer (ENC28J60, W25Q)
middleware/   glue between third-party stacks and our drivers (lwIP port, FatFs port)
third_party/  vendored code, unmodified, with its licence files
docs/         architecture, memory budget, one note per step in docs/steps/
tests/        host unit tests for the pure-C modules
```

## Pin map (NUCLEO-F411RE)

UART, SPI, I2C pins and chip selects come from `app/io/io_board_f411.h`; the rest from `bsp/board.h`.

| Function | Pins |
|---|---|
| Console USART2 (ST-LINK VCP) | PA2 TX, PA3 RX, 115200 8N1 |
| Framed serial link USART1 | PA9 TX, PA10 RX |
| ENC28J60 on SPI2 | SCK PB13, MISO PB14, MOSI PB15, CS PB12, INT PC4 (EXTI4, active low), RESET PC5 |
| W25Q on SPI2 (shared bus, own CS) | CS PB1 |
| I2C1 temperature sensor | SCL PB8 (D15), SDA PB9 (D14) |
| LD2 / B1 | PA5 / PC13 |

## Status

Compile-checked at every step (0 errors, 0 warnings in our code); host tests pass.
Not yet run on hardware.

| Area | Status | Step |
|---|---|---|
| Toolchain, startup, linker script, register blinky | done | 01 |
| 100 MHz clock tree, HAL, console | done | 02 |
| FreeRTOS, heartbeat, hooks | done | 03 |
| Log module (levels per module, log task) | done | 04 |
| SPI2 bus with per-device CS | done | 05 |
| ENC28J60 registers, reset, revision | done | 06 |
| ENC28J60 RX/TX with buffer errata | done | 07 |
| lwIP on FreeRTOS, ENC28J60 netif, static IP / DHCP | done | 08-09 |
| Sensor task, sampling timer, readings queue (simulated source) | done | 10 |
| I2C bus driver (`/I2C1/` of the IO layer) | done | IO layer |
| **I2C sensor driver bodies** | **mentee task** | 10 |
| IO layer for UART / SPI / I2C (`app/io/`, `docs/io-layer.md`) | done (compile + host tests) | - |
| Heap-free JSON writer/parser + host tests | done | 11 |
| Framed serial protocol + host tests | done | 12 |
| GET-SET registry + host tests | done | 13 |
| HTTP server (`/temp`, `POST /interval`, JSON errors) + host tests | done | 14 |
| W25Q probe (JEDEC) | done | 15 |
| **W25Q read / page program / sector erase** | **mentee task** | 15 |
| FatFs on W25Q, reading log, `GET /log` (RAM tail works without media) | done (needs step 15 task for persistence) | 16 |
| Gateway state machine, task supervisor, IWDG | done | 17 |
| Memory budget, architecture doc | done | 18 |
| **On-board bring-up evidence** (logic analyser, Wireshark, ping, demo) | **mentee task** | - |

See `docs/memory-budget.md` (flash 19 %, RAM 69 % with the IO layer) and `docs/architecture.md`.

## Documents

| Document | What |
|---|---|
| [docs/architecture-design.md](docs/architecture-design.md) | architecture and design of the whole gateway: requirements, use cases, quality scenarios, modules, interfaces, task model, memory, error handling, verification, traceability, open risks |
| [docs/architecture.md](docs/architecture.md) | short overview: tasks, priorities, layers |
| [docs/io-layer.md](docs/io-layer.md) | peripheral IO layer: board table, transfer modes, ioctl list, how to add a board |
| [docs/memory-budget.md](docs/memory-budget.md) | flash and RAM by owner |
| [docs/demos/README.md](docs/demos/README.md) | optional demos and their sizes |
| [docs/steps/README.md](docs/steps/README.md) | step-by-step walkthrough of the history |

## Licences

Third-party code keeps its own licence; see `third_party/README.md`.
The licence for the project's own code is to be added by the owner before the repository is made public.
