# Step 05 - SPI2 bus driver with per-device chip select

## What we are solving
Two chips share SPI2: the ENC28J60 (network) and the W25Q (storage). They will be used
from different tasks, so access must be serialised and each chip must only see its own CS.

## Why
- One mutex per bus: a task holds the bus for a whole command sequence. The ENC28J60
  needs several CS cycles back to back (bank select, then register access).
- Chip selects are driven high in `board_init()` before the SPI clock runs, so neither chip
  decodes noise as a command at power-up.
- Byte transfers are polled on the registers with a per-byte timeout. At 12.5 MHz one byte
  takes 0.64 us; a DMA set-up would cost more than it saves for 2-3 byte commands.

## What changed
- `drivers/spi_bus.c/.h` - `spi_bus_init`, `spi_bus_lock/unlock` (static mutex),
  `spi_dev_select/deselect` (waits for BSY before raising CS), `spi_bus_transfer`
  (full duplex, NULL tx sends 0xFF), `spi_dev_xfer` (one full CS cycle).
- `bsp/board.h/.c` - SPI2 pins PB13/PB14/PB15 (AF5), ENC28J60 CS PB12, INT PC4, RESET PC5,
  W25Q CS PB1, `HAL_SPI_MspInit`.
- `bsp/stm32f4xx_hal_conf.h` - SPI module on; `Makefile` builds `stm32f4xx_hal_spi.c`.
- `app/log/log.h` - new `spi` module tag.

## How to verify
```
.\build.ps1 clean all
```
Console:
```
[I][spi] SPI2 ready, 12.5 MHz
```
Mentee check: probe SCK on PB13 with a logic analyser while a later step talks to the
ENC28J60 and confirm 80 ns per clock and CS low only around each command.
