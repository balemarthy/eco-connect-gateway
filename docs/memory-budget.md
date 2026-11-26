# Memory budget (step 18 build)

Build: `make clean all`, arm-none-eabi-gcc 14.3, `-Os`, default flags (static IP, simulated
sensor, watchdog on). Numbers from `arm-none-eabi-size -A` and `arm-none-eabi-nm -S` on
`build/eco-connect-gateway.elf`; re-measure after every change that adds a task or buffer.

## Totals

| Region | Used | Size | Used % | Headroom |
|---|---|---|---|---|
| Flash (`.isr_vector` + `.text` + `.rodata` + `.data` init) | 91 152 B | 512 KB | 17.4 % | ~421 KB |
| RAM (`.data` + `.bss` + reserved heap/stack) | 88 840 B | 128 KB | 67.8 % | ~39 KB |

```
   text    data     bss     dec     hex filename
  91016     128   88720  179864   2be98 build/eco-connect-gateway.elf
```
Sections: `.text` 77 128, `.rodata` 13 472, `.data` 120, `.bss` 86 160,
`._user_heap_stack` 2 560 (main stack 2 KB + newlib heap reserve 512 B, check only).

## RAM by owner (static, from the map)

| Owner | Item | Bytes |
|---|---|---|
| lwIP | `memp_memory_PBUF_POOL_base` (10 x 1536 + headers) | 15 523 |
| lwIP | `ram_heap` (MEM_SIZE 10 KB) | 10 259 |
| lwIP | other pools (TCP PCB/SEG, NETCONN, PBUF, ARP queue, TCPIP msgs, UDP) + ARP table | ~2 600 |
| FreeRTOS | `ucHeap` (heap_4, configTOTAL_HEAP_SIZE) | 16 384 |
| FreeRTOS | idle stack 512 + timer stack 1 024 + timer queue + ready lists | ~2 000 |
| Task stacks | http 4 096, proto 3 072, storage 3 072, app 2 048, sup 2 048, enc_rx 2 048, hb 1 536, sensor 1 536, log 1 024 | 20 480 |
| FatFs | `FATFS` object incl. 4 KB window (`FF_FS_TINY`) | 4 144 |
| FatFs | `f_mkfs` work buffer (used once on a blank chip) | 4 096 |
| HTTP | request 1 024 + response body 1 536 + header 256 | 2 816 |
| Log | queue 16 x 122 B lines | 1 952 |
| Serial link | RX stream buffer 513, frame decoder 224, TX frame 428, packets 408, result 201 | ~1 800 |
| Console | TX ring buffer | 512 |
| Queues | readings 8 x 20, storage 8 x 20, FSM events 16 | ~350 |
| newlib | `__sf` stdio structures (pulled by snprintf) | 312 |

## FreeRTOS heap (heap_4, 16 KB)
Our own tasks, queues, timers and semaphores are all static. The heap serves what the lwIP
FreeRTOS port creates at run time:
- tcpip thread: 2 048 B stack + TCB, its 8-entry mailbox, the core-lock mutex;
- per netconn: receive mailbox (6), op-completed semaphore; the listener adds an accept
  mailbox (4). Created on `netconn_new`/accept, returned on `netconn_delete`.

Estimate: about 3.5 KB in steady state with the listener and one connection. Measure on
the board: `curl http://<ip>/status` reports `heap_free` and `heap_min`
(`xPortGetMinimumEverFreeHeapSize`). If `heap_min` stays above 8 KB after a load test,
`configTOTAL_HEAP_SIZE` can drop to 8 KB.

## Where the headroom goes next
- ~39 KB free RAM. Candidates for Project 2: an event log buffer, littlefs instead of FatFs
  (smaller cache), more PBUFs if the RX overflow counter rises under load.
- Cheap savings if needed: `f_mkfs` work buffer could reuse the HTTP body buffer (-4 KB);
  `PBUF_POOL_SIZE` 10 -> 6 (-6 KB) at 10 Mbit/s half duplex; stack sizes after measuring
  high-water marks (`uxTaskGetStackHighWaterMark`) on hardware.
- Stack sizes are first estimates (formatter + buffers per task). Before cutting any of
  them, measure the high-water marks under load.
