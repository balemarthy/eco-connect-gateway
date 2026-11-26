# Step 16 - FatFs on W25Q with reading log

## What we are solving
Keep readings across a reset and let a client fetch the recent ones (`GET /log`), without
making the gateway depend on the flash being there.

## Why
- FatFs gives a file you can also read on a PC later; one FAT logical sector = one 4 KB
  W25Q erase sector, so a sector write is erase + 16 page programs and never disturbs a
  neighbour.
- Flash wear: every `f_sync` rewrites at least one 4 KB sector, so the task syncs every
  16 lines, and rotates `READINGS.LOG` to `READINGS.OLD` at 256 KB.
- Only the storage task calls FatFs (`FF_FS_REENTRANT 0`). HTTP never touches the file:
  `GET /log` reads a RAM copy of the last 8 readings through the registry (`history`).
- Graceful degradation: no chip, or the W25Q data path from step 15 not written yet, gives
  `[W][fs] no media, logging off`; readings and `GET /log` keep working from RAM.
- `FF_FS_TINY 1` makes files share the volume window: one 4 KB buffer instead of two.

## What changed
- `third_party/fatfs/` - FatFs R0.16 (`ff.c`, `ff.h`, `diskio.h`, `ffsystem.c`,
  `ffunicode.c`, `LICENSE.txt`); only `ff.c` is compiled (no long file names).
- `middleware/fatfs_port/ffconf.h` - mkfs on, 4096-byte sectors, tiny buffers, no RTC.
- `middleware/fatfs_port/diskio.c` - status/init/read/write/ioctl on `w25q_*`.
- `app/storage/storage_task.c/.h` - queue from the app task, mount (format a blank chip),
  append JSON lines, periodic sync, rotation, RAM tail, registry key `history`.
- `app/core/app_task.c` - hands every reading to `storage_submit()` (non-blocking).
- `app/http/http_router.c` - `GET /log`. `tests/test_http.c` - `/log` cases (82 checks).

## How to verify
```
make test
.\build.ps1 clean all
```
Console without a working flash driver:
```
[W][fs] no media, logging off
```
After the step 15 mentee task, on a blank W25Q:
```
[I][fs] no file system, formatting
[I][fs] mounted, READINGS.LOG 0 bytes
```
```
curl http://192.168.1.50/log
{"persisted":true,"lines":40,"records":[{"node":1,"value":23.13,"timestamp":...,"seq":33},...]}
```
Mentee check: reset the board, then confirm the file size printed at mount grew.
