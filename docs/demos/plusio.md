# Demo: IO layer API (`make DEMO=plusio`)

A small task that exercises the IO layer (`app/io/`, design in [../io-layer.md](../io-layer.md))
next to the gateway tasks, on the same handles they use. The IO layer itself is part of every
build; this demo only adds the task `app/demos/plusio_demo.c`. It only logs (tag `demo`, lines
start with `pio:`); nothing in the gateway depends on it. The demo name stays `plusio` so existing
build commands keep working.

| When | Path | What it does | Shows |
|---|---|---|---|
| once at start | - | `io_open("/UART9/")`; circular-buffer RX on `/SPI2/`; speed 300 on `/UART1/`; I2C address 0x03 on `/I2C1/` | request checks: `NULL`, `unsupported`, `bad parameter`, `bad parameter` |
| once at start | `/UART2/` | `IO_IOCTL_USE_CHAR_QUEUE_RX` (64), RX timeout 500 ms | switching one direction's mode at run time; TX stays the log task's character queue |
| every 500 ms | `/UART2/` | `io_read` up to 15 keys typed on the ST-LINK terminal, logged as `pio: keys "..."` | character-queue RX, short count on timeout |
| every 5 s | `/SPI2/` | bus mutex, `SPI_SELECT(IO_SPI2_CS_W25Q)`, `io_write(9Fh)`, `io_read(3)`, deselect, release | zero copy on a shared bus, per-device chip select, while the ENC28J60 and storage task use it |
| every 5 s | `/I2C1/` | `I2C_SET_SLAVE_ADDRESS(0x48)`, `I2C_WRITE_READ` register 0, 2 bytes | real I2C transfer; without a sensor the log shows `io error` (NACK) |
| every 5 s | `/UART1/` | `IO_IOCTL_GET_STATS` of the protocol link | statistics from the RX interrupt (overruns, errors) |

Expected log on a board with a W25Q64 fitted and no I2C sensor (not run yet):
```
[I][demo] pio: open /UART9/ -> NULL (not in the table)
[I][demo] pio: /SPI2/ circular buffer RX -> unsupported
[I][demo] pio: /UART1/ speed 300 -> bad parameter
[I][demo] pio: /I2C1/ address 0x03 -> bad parameter
[I][demo] pio: /UART2/ RX in character-queue mode: ok - type on the console
[I][demo] pio: /SPI2/ W25Q JEDEC EF 40 17
[I][demo] pio: /I2C1/ 0x48 reg 0: io error
[I][demo] pio: /UART1/ rx 0 tx 0 overruns 0 errors 0
```

Cost over `DEMO=none`: see [README.md](README.md). Task: 4 KB static stack (`DEMO_TASK_STACK_WORDS`),
priority 2, supervisor window 3 s (it checks in every 500 ms).

Earlier the demo was a skeleton adapter in `app/plusio/` over the old bus drivers; both are
replaced by the IO layer, which the whole firmware now uses.
