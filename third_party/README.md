# third_party

Vendored code. Files here are copied unmodified from their upstream release and keep their
licence files and headers. Project changes never go in this folder: configuration lives in
`app/` (FreeRTOSConfig.h), `bsp/` (stm32f4xx_hal_conf.h) and `middleware/` (lwipopts.h, ffconf.h).

| Folder | Component | Version | Licence | Added in |
|---|---|---|---|---|
| `cmsis/` | Arm CMSIS-Core(M) headers + ST CMSIS device headers for STM32F4xx | CMSIS-Core 5.1 (core_cm4 V5.0.8), device 2.6.8 | Apache-2.0 | step 01 |
| `stm32f4xx_hal/` | STM32F4 HAL drivers (subset) | 1.8.1 | BSD-3-Clause | step 02 |
| `freertos/` | FreeRTOS-Kernel (core, GCC/ARM_CM4F port, heap_4) | V11.1.0 | MIT | step 03 |
| `lwip/` | lwIP TCP/IP stack (`src/` + FreeRTOS `sys_arch` from contrib) | 2.2.0 | BSD-3-Clause | step 08 |
| `fatfs/` | FatFs generic FAT file system module | R0.16 | FatFs licence (BSD-style, 1 clause) | step 16 |

Only the parts that the build needs are copied. FatFs: the template `ffconf.h` and
`diskio.c` are not copied; ours are in `middleware/fatfs_port/` (an `ffconf.h` next to `ff.h`
would be picked first by `#include "ffconf.h"`). The table is the single place to look up
versions when you update a component.
