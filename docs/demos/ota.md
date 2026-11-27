# Demo: OTA image download (`make DEMO=ota`)

Downloads a firmware image from an HTTP server on the LAN in fixed blocks (HTTP `Range`),
reassembles it in a store and verifies it. **Board-independent by design:** no flash write,
no bootloader, no bank swap. `ready` means "a verified image is stored", not "installed".

## Modules (`app/ota/`)
| File | Role | Host tested |
|---|---|---|
| `ota_fsm.c/.h` | event-driven state machine IDLE -> REQUESTED -> DOWNLOADING -> VERIFYING -> READY / FAILED | yes (`tests/test_ota.c`) |
| `ota_reasm.c/.h` | block bitmap: out of order, duplicates ignored, exact block length, size limits | yes |
| `ota_crc32.c/.h` | CRC-32 (IEEE, zlib-compatible), nibble table | yes |
| `ota_storage.h` | store interface (open / write / read / close / abort / capacity) | - |
| `ota_storage_ram.c/.h` | RAM store (default; 16 KB static buffer on the target) | yes |
| `ota_storage_fatfs.c/.h` | file `OTA.BIN` on the FatFs volume (optional, see limits) | compiled only |
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
| | `GET ota` -> `{"state":"downloading","reason":"none","size":40960,"blocks":40,"received":12,"dups":0,"rejected":0,"retries":0,"crc_expected":"1a2b3c4d","crc":"00000000"}` |
| HTTP | `POST /ota {"url":"http://192.168.1.10:8080/fw.bin","crc32":"1a2b3c4d"}` (url optional), `POST /ota {"cmd":"abort"}`, `GET /ota` |

Setters run in the caller's task and only queue a command; the OTA task owns the state
machine and copies a snapshot for `GET ota` under a short critical section.

## Transport
Per block: new TCP connection, `GET <path>` with `Range: bytes=<first>-<last>`,
`Connection: close`. Block 0 also gives the total size (`Content-Range: bytes 0-1023/<total>`).
A server without Range support works only if the file fits one block (200 + whole body).
Each request has a 5 s receive timeout and up to 3 tries; then the download fails with
`transport error`. URL host must be an IPv4 literal (no DNS), plain http only.

## Settings (`app/config/app_config.h`)
`OTA_SERVER_HOST/PORT/PATH` (default URL), `OTA_BLOCK_SIZE` 1024, `OTA_MAX_IMAGE_SIZE` 16 KB
(RAM store), `OTA_STORAGE` RAM | FATFS, `OTA_MAX_IMAGE_SIZE_FATFS` 256 KB, `OTA_HTTP_TIMEOUT_MS`,
`OTA_BLOCK_RETRIES`.

## Cost (measured)
+6,908 B flash, +22,664 B RAM over `DEMO=none` (16 KB image buffer, 1 KB block buffer, 4 KB task
stack, 512 B head buffer). RAM use is then 85 % of 128 KB; for larger images use the FatFs store.

## Limits / next steps
- FatFs store: FatFs is built non-reentrant because only the storage task uses it. Enable
  `FF_FS_REENTRANT` or route through the storage task first; W25Q read/program are mentee tasks.
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
The image must fit `OTA_MAX_IMAGE_SIZE` (16 KB) with the RAM store; a full gateway image
(~95 KB) needs the FatFs store or a larger RAM buffer.
