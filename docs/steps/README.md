# Step-by-step walkthrough

The history of `main` is the course: one commit per step, in order. For each step:

```
git checkout <hash>            # e.g. git checkout 7a5904b
.\build.ps1 clean all          # firmware (from step 01)
make test                      # host tests (from step 11)
```
read `docs/steps/NN-*.md` of that commit, do the checks on the board, then go to the next one.
`git checkout main` brings you back to the latest state.

| # | Commit | Title | What it solves | How to verify |
|---|---|---|---|---|
| 00 | `f7e8f3f` step 00: repository skeleton and layout | Repository skeleton | Where every kind of code lives; third-party kept apart | `git ls-files` matches the README layout |
| 01 | `547f610` step 01: bare-metal register blinky with hand-written Makefile | Toolchain + register blinky | Source to image with our own Makefile and linker script; no HAL, no RTOS | build shows `text/data/bss`; LD2 blinks ~1 Hz |
| 02 | `cd86484` step 02: 100 MHz clock tree, HAL and UART console | Clocks, HAL, console | 100 MHz from the ST-LINK 8 MHz, polled console | `Eco-Connect boot`, `SYSCLK=100000000` |
| 03 | `7a5904b` step 03: FreeRTOS V11.1.0 with heartbeat task | FreeRTOS | Tasks; SysTick to the kernel, HAL tick to TIM11; fault hooks | `FreeRTOS V11.1.0`, `scheduler started` |
| 04 | `891ec32` step 04: per-module log levels over a log task | Log module | Non-blocking logging, level per module, IRQ console TX | `[I][main] boot`, `[D][hb] tick n`, B1 toggles |
| 05 | `522a220` step 05: SPI2 bus driver with per-device chip select | SPI bus | One bus, two chips, a mutex and a CS each | `[I][spi] SPI2 ready, 12.5 MHz` |
| 06 | `013ad47` step 06: ENC28J60 register access, reset and revision check | ENC28J60 registers | Banks, dummy byte, PHY access, reset wait, identity check | `[I][enc] rev B7` (see erratum 1) |
| 07 | `47506f2` step 07: ENC28J60 RX/TX with buffer errata handled | ENC28J60 packets | RX at 0000h, odd ERXRDPT, PKTDEC, TXRST, TSV, INT -> task | `rx frames=N tx=M` rising (see erratum 1) |
| 08 | `ca69e6a` step 08: lwIP 2.2.0 on FreeRTOS | lwIP core | TCP/IP stack on the RTOS, sized for 128 KB | `[I][net] tcpip thread up (lwIP 2.2.0)` |
| 09 | `2b5b762` step 09: ENC28J60 lwIP netif, static IP and DHCP | netif | Driver meets stack: ARP, ping, link events | `netif up 192.168.1.50`; `ping` replies |
| 10 | `9efe59c` step 10: sensor task, sampling timer and readings queue | Sensor + queue | Timer -> task -> static queue -> app, no heap | `[I][app] seq=1 node=1 t=23.00C`; mentee: I2C driver |
| 11 | `98736c4` step 11: heap-free JSON writer and parser with host tests | JSON | JSON without malloc, proven on the PC | `make test`: test_json PASS; `[I][app] json {...}` |
| 12 | `e93dcef` step 12: Eco-Connect framed serial protocol task | Serial protocol | Outer frame + inner packet, one checksum rule both ways | test_serial_protocol PASS; `frame ok op=GET`, `bad CS` |
| 13 | `39147df` step 13: GET-SET command registry | Registry | One command table for every interface | test_registry PASS; `GET interval` over the link |
| 14 | `496d927` step 14: HTTP server with /temp and /interval | HTTP server | `GET /temp`, `POST /interval`, JSON errors | test_http PASS; `curl http://<ip>/temp` |
| 15 | `72e02f4` step 15: W25Q SPI NOR driver interface | W25Q contract | NOR rules as an API; probe given, data path = mentee task | `JEDEC EF4017` or `not present` (see erratum 1) |
| 16 | `81d916f` step 16: FatFs on W25Q with reading log | FatFs | Reading log on flash, graceful without media, `GET /log` | `[W][fs] no media, logging off` or `mounted` (see erratum 1) |
| 17 | `5f910e1` step 17: gateway state machine and task watchdog supervisor | FSM + watchdog | Visible state, every task watched, IWDG reset on a stall | test_fsm PASS; `IDLE -> SAMPLING`; B1 held 3 s -> reset |
| 18 | `90c9145` step 18: integration, memory budget and architecture doc | Integration | Does it fit, and how do the parts talk | flash 17 %, RAM 68 %; `docs/memory-budget.md` |
| - | `8b54a39` docs: wording cleanup | (no new feature) | Comment/doc wording only, plus one parameter rename in `bsp/clock.c` | same build output as step 18 |

The commit that adds this index comes last and cannot list its own hash.

## Errata (found later, fixed in a later step - history is not rewritten)
1. **`HAL_Delay()` before the scheduler (steps 06, 07, 15, 16).** The first FreeRTOS call in
   `main()` (the log queue) raises BASEPRI until `vTaskStartScheduler()`. The HAL tick on
   TIM11 is masked in that window, so the `HAL_Delay()` inside `enc28j60_init()` (steps 06-07)
   and `w25q_init()` (steps 15-16), both called from `main()`, never return on hardware.
   Fixed in step 17 (the chip bring-up runs after the scheduler starts; the pre-scheduler
   fallback is a DWT busy wait). Step 09 had already moved the ENC28J60 init into the tcpip
   thread. On those four steps, move the init call after `vTaskStartScheduler()` (into a task)
   to try them on the board. Good exercise: explain why the polled console still works there.
2. **Bare `make` (steps 11-16)** ran the host tests because the `test:` rule came before
   `all:`. Use `make clean all` / `.\build.ps1 clean all` there (as every note says);
   fixed in step 17 with `.DEFAULT_GOAL := all`.

## After step 18
The step notes describe the code as it was at each step. Later changes on `main`:
- Optional demos (`make DEMO=...`): [../demos/README.md](../demos/README.md).
- **IO layer** (`app/io/`, [../io-layer.md](../io-layer.md)): the console, link, SPI and I2C
  drivers of steps 02, 04, 05, 10 and 12 (`drivers/uart_console.c`, `uart_link.c`, `spi_bus.c`,
  `i2c_bus.c`) are replaced by `/UART2/`, `/UART1/`, `/SPI2/` and `/I2C1/` of one
  open/read/write/ioctl API with a board table. Same behaviour: interrupt-driven console TX, an RX
  ring for the link, one bus mutex and one chip select per SPI device. The I2C bus driver is now
  implemented (`app/io/io_i2c.c`).
- **Architecture and design document** for the whole gateway: [../architecture-design.md](../architecture-design.md).

## Left for mentees
- Step 10: the I2C sensor source in `app/sensor/sensor_task.c`, on `/I2C1/` of the IO layer.
- Step 15: `w25q_read()`, `w25q_page_program()`, `w25q_sector_erase()`.
- On-board evidence: logic-analyser capture of the ENC28J60 init, Wireshark capture of an
  HTTP request and response, ping, stack high-water marks, demo video.
