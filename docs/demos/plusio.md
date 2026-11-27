# Demo: Plus-IO style peripheral adapter (`make DEMO=plusio`) - skeleton

**Status: compiling skeleton with TODOs.** FreeRTOS+IO itself is not usable here: the only
existing code is V1.0.1 (2012) for NXP LPC17xx (`/UART3/`, `/SSP1/`, `/I2C2/` on the LPCXpresso
base board), it is GPLv2 / commercial dual-licensed, and upstream FreeRTOS carries only a
`readme.txt` + link for it. Its common layer would compile against V11.1.0 (pre-V8 type names
are still mapped because `configENABLE_BACKWARD_COMPATIBILITY` is 1), but there is no STM32
device layer and it allocates descriptors with `pvPortMalloc()`.

So `app/plusio/` keeps the +IO **call shape** in our own static code, on top of the project
drivers (which keep the bus mutexes, chip selects and ISRs):

```
pio_open("/SPI2/") / pio_read / pio_write / pio_ioctl
        |
app/plusio/plusio.c   static table: path, type, timeouts, chip select / I2C address
   /SPI2/  -> drivers/spi_bus.c      done: each read/write/XFER = one locked CS cycle
   /I2C1/  -> drivers/i2c_bus.c      wired; i2c_bus.c bodies are the mentee stubs (NOT_READY)
   /UART1/ -> drivers/uart_link.c    TODO: single reader/writer today (protocol task)
   /UART2/ -> drivers/uart_console.c TODO: single writer today (log task)
```

| ioctl | +IO name | Here |
|---|---|---|
| `PIO_IOCTL_SET_TX_TIMEOUT` / `SET_RX_TIMEOUT` | `ioctlSET_TX/RX_TIMEOUT` | per descriptor, ms |
| `PIO_IOCTL_OBTAIN/RELEASE_WRITE_MUTEX` | same | SPI2: `spi_bus_lock()` / `spi_bus_unlock()`, several CS cycles under one lock |
| `PIO_IOCTL_I2C_SET_SLAVE_ADDR` | `ioctlSET_I2C_SLAVE_ADDRESS` | 7-bit |
| `PIO_IOCTL_SET_SPEED` | `ioctlSET_SPEED` | TODO, returns unsupported (speeds fixed in drivers) |
| `PIO_IOCTL_SPI_SET_DEVICE`, `PIO_IOCTL_SPI_XFER`, `PIO_IOCTL_GET_LAST_STATUS` | - | ours: chip select, write-then-read in one CS cycle, error of the last read/write |
| transfer modes (zero copy, char queue, circular buffer), `SET_INTERRUPT_PRIORITY` | - | dropped: one fixed mode per driver, NVIC priorities fixed in `bsp/` |

Demo task `pio` (`app/plusio/plusio_demo.c`, supervisor window 3 s), every 5 s:
`/SPI2/` JEDEC ID of the W25Q (`[I][demo] pio: /SPI2/ JEDEC EF 40 16` on a W25Q32),
`/I2C1/` 2-byte read at 0x48 (`not ready` until the I2C driver exists), once `/UART1/`
write (`busy (TODO)`).

Cost (measured): +1,572 B flash, +4,288 B RAM over `DEMO=none`.

## TODO (mentee tasks)
1. `uart_link.c`: TX mutex, then let `/UART1/` writes through; RX hand-over from the protocol task.
2. `i2c_bus.c`: implement the driver (contract in `i2c_bus.h`); `/I2C1/` then works unchanged.
3. `PIO_IOCTL_SET_SPEED`: add `spi_bus_set_prescaler()` / I2C timing to the drivers first.
4. Move the sensor driver onto `pio_open("/I2C1/")` if the +IO style is wanted for drivers.
