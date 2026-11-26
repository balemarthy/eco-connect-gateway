# eco-connect-gateway

Eco-Connect IoT gateway firmware for the **NUCLEO-F411RE** (STM32F411RE, Cortex-M4F, 100 MHz,
512 KB flash / 128 KB RAM). A sensor reading travels from a sampling task, through a static
queue, into an application task and out over Ethernet (ENC28J60 on SPI, lwIP) as JSON, and over
a framed serial link (USART1).

The repository is built one step at a time. Every commit on `main` is one step and carries its
own write-up in `docs/steps/`. Check out a step, read its note, build it, then move on.

## Stack

| Layer | Choice |
|---|---|
| MCU support | CMSIS + STM32F4 HAL (vendored in `third_party/`) |
| RTOS | FreeRTOS-Kernel V11.1.0, GCC/ARM_CM4F port, heap_4 |
| Network | lwIP 2.2.0 (NO_SYS=0) over an ENC28J60 on SPI2 |
| Storage | FatFs R0.16 on a W25Q SPI NOR flash |
| Build | hand-written `Makefile`, arm-none-eabi-gcc 14.3 (STM32CubeIDE 2.2.0 toolchain) |

## Repository layout

```
app/          application: main, tasks, log, json, protocol, registry, http, storage
bsp/          board support: startup, linker script, clocks, pins, interrupts
drivers/      peripheral and device drivers (UART, SPI, ENC28J60, I2C, W25Q)
middleware/   glue between third-party stacks and our drivers (lwIP port, FatFs port)
third_party/  vendored code, unmodified, with its licence files
docs/         architecture, memory budget, one note per step in docs/steps/
tests/        host unit tests for the pure-C modules (from step 11)
```

## Pin map (NUCLEO-F411RE)

| Function | Pins |
|---|---|
| Console USART2 (ST-LINK VCP) | PA2 TX, PA3 RX, 115200 8N1 |
| Framed serial link USART1 | PA9 TX, PA10 RX |
| ENC28J60 on SPI2 | SCK PB13, MISO PB14, MOSI PB15, CS PB12, INT PC4 (EXTI4, active low), RESET PC5 |
| W25Q on SPI2 (shared bus, own CS) | CS PB1 |
| I2C1 temperature sensor | SCL PB8 (D15), SDA PB9 (D14) |
| LD2 / B1 | PA5 / PC13 |

## Status

Step 00: layout only. The status table is filled in at step 18.

## Licences

Third-party code keeps its own licence; see `third_party/README.md`.
The licence for the project's own code is to be added by the owner before the repository is made public.
