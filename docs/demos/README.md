# Optional demos

Built on top of the gateway with `make DEMO=<name>` (Windows: `.\build.ps1 -Demo <name>`).
Exactly one demo task is added; `main()` calls its `demo_start()` after the gateway tasks.
`DEMO=none` (default) compiles none of this: same sources, same binary as without the switch.
All settings are in `app/config/app_config.h`; each demo builds into `build/demo-<name>/`.

| DEMO | Note | Status | Flash / RAM | over none |
|---|---|---|---|---|
| `tcp_echo` | [tcp_echo.md](tcp_echo.md) | done | 99,200 B / 94,416 B | +628 B / +4,200 B |
| `mqtt` | [mqtt.md](mqtt.md) | done (coreMQTT v2.3.1+, plain TCP) | 114,924 B / 95,128 B | +16,352 B / +4,912 B |
| `plusio` | [plusio.md](plusio.md) | done: demo task of the IO layer API (`docs/io-layer.md`) | 100,048 B / 94,408 B | +1,476 B / +4,192 B |
| `ota` | [ota.md](ota.md) | done for download + verify (FatFs store default); no install / bootloader | 106,732 B / 94,040 B | +8,160 B / +3,824 B |

Sizes: arm-none-eabi-gcc 14.3, `-Os`, clean build each, against `DEMO=none` = 98,572 B flash /
90,216 B RAM (with the IO layer). RAM of 128 KB: 69 % for `none`, 72-73 % for the demos.
Nothing here has been run on hardware yet.

Host tests that cover demo code: `tests/test_ota.c` (state machine, reassembly, CRC32, RAM store),
`tests/test_ota_http.c` (URL, Range request, response head, `POST/GET /ota` route) and
`tests/test_io.c` (the IO layer the `plusio` demo exercises).

Common rules the demos follow: static allocation only (task, stack, queues, buffers), every
blocking call has a timeout, the task checks in with the supervisor inside its window, log lines
use the `demo` tag (exists only in demo builds), results go through the registry so the serial
link and HTTP see the same values.
