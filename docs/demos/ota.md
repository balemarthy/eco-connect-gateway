# Demo: OTA image download (`make DEMO=ota`)

Downloads a firmware image from an HTTP server on the LAN in fixed blocks (HTTP `Range`),
reassembles it in a store (a FatFs file on the W25Q by default) and verifies it. **Board-independent by design:** no flash write,
no bootloader, no bank swap. `ready` means "a verified image is stored", not "installed".

## Modules (`app/ota/`)
| File | Role | Host tested |
|---|---|---|
| `ota_fsm.c/.h` | event-driven state machine IDLE -> REQUESTED -> DOWNLOADING -> VERIFYING -> READY / FAILED | yes (`tests/test_ota.c`) |
| `ota_reasm.c/.h` | block bitmap: out of order, duplicates ignored, exact block length, size limits | yes |
| `ota_crc32.c/.h` | CRC-32 (IEEE, zlib-compatible), nibble table | yes |
| `ota_storage.h` | store interface (open / write / read / close / abort / capacity) | - |
| `ota_storage_fatfs.c/.h` | file `OTA.BIN` on the FatFs volume (default); every FatFs call runs in the storage task through `storage_call()` | compiled only |
| `ota_storage_ram.c/.h` | RAM store (`make DEMO=ota OTA_STORE=ram`, 16 KB static buffer) | yes |
| `ota_http_msg.c/.h` | URL parse, Range request text, response-head parse | yes (`tests/test_ota_http.c`) |
| `ota_http.c/.h` | one GET + Range per block over lwIP netconn | compiled only |
| `ota_task.c` | OTA task, command queue, registry keys `ota` / `ota_url`, snapshot for GET | compiled only |
| `app/http/http_router.c` | `GET /ota`, `POST /ota` (only with `APP_DEMO_OTA`) | yes (`tests/test_ota_http.c`) |

## State machine
```
          REQUEST(crc)          BEGIN(size)             last BLOCK            VERIFY
 IDLE ---------------> REQUESTED -----------> DOWNLOADING -----------> VERIFYING -------> READY
                           |                     | BLOCK: any order,       |
                           | ERROR / ABORT       | duplicates ignored      | size / CRC wrong
                           v                     v ERROR/ABORT/store error v
                        FAILED <------------------------------------------+
 READY / FAILED: REQUEST starts again, RESET -> IDLE.  Events invalid in a state are ignored.
```
Verification: every block present, the store holds exactly the announced size, CRC-32 over
the stored bytes equals the CRC given with `start`. SHA-256 is not built (`OTA_VERIFY_SHA256 0`);
adding it means one more check in `verify()` with a small public-domain SHA-256 and the digest
passed with `start`.

## Commands
| Interface | Command |
|---|---|
| serial / registry | `SET ota_url http://192.168.1.10:8080/fw/eco-connect-gateway.bin` |
| | `SET ota start 1a2b3c4d` (CRC-32 of the file, 8 hex digits), `SET ota abort`, `SET ota reset` |
| | `GET ota` -> `{"state":"downloading","reason":"none","size":40960,"blocks":80,"received":24,"dups":0,"rejected":0,"retries":0,"crc_expected":"1a2b3c4d","crc":"00000000"}` |
| HTTP | `POST /ota {"url":"http://192.168.1.10:8080/fw.bin","crc32":"1a2b3c4d"}` (url optional), `POST /ota {"cmd":"abort"}`, `GET /ota` |

Setters run in the caller's task and only queue a command; the OTA task owns the state
machine and copies a snapshot for `GET ota` under a short critical section.

## Transport
Per block: new TCP connection, `GET <path>` with `Range: bytes=<first>-<last>`,
`Connection: close`. Block 0 also gives the total size (`Content-Range: bytes 0-511/<total>`).
A server without Range support works only if the file fits one block (200 + whole body).
Each request has a 5 s receive timeout and up to 3 tries; then the download fails with
`transport error`. URL host must be an IPv4 literal (no DNS), plain http only.

## Settings (`app/config/app_config.h`)
`OTA_SERVER_HOST/PORT/PATH` (default URL), `OTA_BLOCK_SIZE` 512, `OTA_STORAGE` FATFS (default) |
RAM (`make DEMO=ota OTA_STORE=ram`), `OTA_MAX_IMAGE_SIZE_FATFS` 256 KB, `OTA_MAX_IMAGE_SIZE` 16 KB
(RAM store only), `OTA_TASK_STACK_WORDS` 512 (2 KB), `OTA_FATFS_CALL_TIMEOUT_MS` 5000,
`OTA_HTTP_TIMEOUT_MS`, `OTA_BLOCK_RETRIES`. Any of them: `make DEMO=ota EXTRA_DEFS="-DOTA_BLOCK_SIZE=1024"`.

## Cost (measured) and RAM trim
| Build | Flash | RAM | RAM % of 128 KB |
|---|---|---|---|
| before the trim (RAM store, 1 KB blocks, 4 KB task stack) | 98,060 B | 111,504 B | 85.1 % |
| `make DEMO=ota` (FatFs store, 512 B blocks, 2 KB task stack) | 99,488 B | 92,888 B | 70.9 % |
| `make DEMO=ota OTA_STORE=ram` (16 KB RAM store, same block and stack) | 98,344 B | 109,160 B | 83.3 % |
| `make DEMO=ota` after the IO layer (`docs/io-layer.md`, +1,160 B RAM in every build) | 106,732 B | 94,040 B | 71.7 % |

Where the 18,616 B went (default build against the old one):
- image buffer: the 16 KB RAM store is no longer linked; the FatFs store needs a `FIL`
  (FatFs is built with `FF_FS_TINY 1`, so a `FIL` has no own sector buffer) and its request fields;
- block buffer 1024 -> 512 B (`OTA_BLOCK_SIZE`); the block bitmap grows to 64 B (512 blocks of 256 KB);
- OTA task stack 4096 -> 2048 B (`OTA_TASK_STACK_WORDS`, see below);
- the storage task gained the `storage_call()` path (+216 B RAM, also in `DEMO=none`).

The RAM store stays as a bench option for a board without a working W25Q; it is above the 75 %
RAM line and should not be the build that ships.

### OTA task stack
Static estimate, not a measurement on the board: `make DEMO=ota OPT="-Os -g3 -fstack-usage"`,
then the worst path over the direct-call graph of the ELF (`objdump -d`), with the store calls
added by hand because they go through function pointers:

| Path | Bytes |
|---|---|
| `ota_task` (start/download inlined) 312 -> `fetch` 64 -> `ota_http_get_range` 72 -> `netconn_recv` ... `xQueueSemaphoreTake` -> `vAssertCalled` | 792 |
| `ota_task` 312 -> `log_write` 152 -> `vsnprintf` (newlib nano, no `.su` data, about 300) | ~ 760 |
| context saved on the task stack by a switch with the FPU in use | ~ 200 |

About 1.1 KB worst case, so 2 KB leaves roughly 0.9 KB. FatFs and the W25Q driver are not on this
stack any more: the FatFs store runs them in the storage task (`STORAGE_STACK_WORDS` 768).
`configCHECK_FOR_STACK_OVERFLOW 2` stays on, and every download ends with the log line
`ota: stack never used N of 2048 bytes` (FreeRTOS high-water mark) to confirm the figure on the board.

## Limits / next steps
- FatFs store: the W25Q read/program paths are mentee tasks; until they exist the volume does not
  mount, `open()` fails and a download ends in `failed` / `storage error`. Use `OTA_STORE=ram`
  on the bench until then.
- Verification reads the image back in 64-byte pieces; with the FatFs store each piece is one
  `storage_call()` (a few context switches each), about 4,096 calls for a 256 KB image.
- Installing the image needs a bootloader + two slots (or the F411's sectors 5-7 as a download
  slot), image header with version, and a confirm-after-boot step for rollback. Not in scope.

## Try it (not run in this job)
Serve the image from a static server that answers Range requests (e.g. nginx on
192.168.1.10:8080 with `fw/eco-connect-gateway.bin`). A server that ignores Range and sends
`200` with the whole file only works for images up to one block.
```
python -c "import zlib;print('%08x'%zlib.crc32(open('fw/eco-connect-gateway.bin','rb').read()))"
make DEMO=ota
# serial link: SET ota start <crc>, then GET ota until "state":"ready"
# or: curl -d '{"crc32":"<crc>"}' http://192.168.1.50/ota ; curl http://192.168.1.50/ota
```
With the default FatFs store the image may be up to `OTA_MAX_IMAGE_SIZE_FATFS` (256 KB), so a full
gateway image (~100 KB) fits; with `OTA_STORE=ram` it must fit `OTA_MAX_IMAGE_SIZE` (16 KB).
