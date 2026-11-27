# Optional demos

Built on top of the gateway with `make DEMO=<name>` (Windows: `.\build.ps1 -Demo <name>`).
Exactly one demo task is added; `main()` calls its `demo_start()` after the gateway tasks.
`DEMO=none` (default) compiles none of this: same sources, same binary as without the switch.
All settings are in `app/config/app_config.h`; each demo builds into `build/demo-<name>/`.

| DEMO | Note | Status | Flash / RAM over none |
|---|---|---|---|
| `tcp_echo` | [tcp_echo.md](tcp_echo.md) | done | +628 B / +4,200 B |
| `mqtt` | [mqtt.md](mqtt.md) | done (coreMQTT v2.3.1+, plain TCP) | +16,348 B / +4,920 B |
| `plusio` | [plusio.md](plusio.md) | skeleton: SPI2 works, I2C1 on the stub driver, UARTs TODO | +1,572 B / +4,288 B |
| `ota` | [ota.md](ota.md) | done for download + verify (FatFs store default); no install / bootloader | +8,160 B / +3,832 B |

Sizes: arm-none-eabi-gcc 14.3, `-Os`, measured against `DEMO=none` = 91,328 B flash / 89,056 B RAM
(the other rows are still against the earlier 91,152 / 88,840; updated with the IO layer).
Nothing here has been run on hardware yet.

Host tests that cover demo code: `tests/test_ota.c` (state machine, reassembly, CRC32, RAM store)
and `tests/test_ota_http.c` (URL, Range request, response head, `POST/GET /ota` route).

Common rules the demos follow: static allocation only (task, stack, queues, buffers), every
blocking call has a timeout, the task checks in with the supervisor inside its window, log lines
use the `demo` tag (exists only in demo builds), results go through the registry so the serial
link and HTTP see the same values.
