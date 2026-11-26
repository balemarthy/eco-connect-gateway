# Step 08 - lwIP 2.2.0 on FreeRTOS

## What we are solving
We can move raw frames; now we need ARP, IP, ICMP, UDP, TCP and DHCP. lwIP provides them.
This step brings the stack up on FreeRTOS without a network interface yet.

## Why
- `NO_SYS = 0`: lwIP runs its own `tcpip` thread and offers the sequential `netconn` API,
  which keeps the HTTP server (step 14) as plain blocking code.
- `LWIP_TCPIP_CORE_LOCKING = 1`: API calls take a lock instead of posting a message to the
  tcpip thread for every call - fewer context switches.
- Memory is sized for a 128 KB part: 10 KB lwIP heap, 10 x 1536 B RX pool, 4 TCP PCBs.
  All of it is static arrays inside lwIP; nothing grows at run time.
- `LWIP_RAND()` uses our own xorshift generator. newlib `rand()` would allocate its state
  on the heap the first time it is called.

## What changed
- `third_party/lwip/` - lwIP 2.2.0 (BSD-3-Clause, `COPYING`): `src/core`, `src/api`,
  `src/include`, `src/netif/ethernet.c`, plus the FreeRTOS port from `contrib/ports/freertos`
  (`sys_arch.c`, `arch/sys_arch.h`), unmodified.
- `middleware/lwip_port/lwipopts.h` - stack options and sizes.
- `middleware/lwip_port/arch/cc.h` - errno, byte order, assert and random hooks.
- `middleware/lwip_port/port_misc.c` - `lwip_port_assert()`, `lwip_port_rand()`.
- `app/net/net_init.c/.h` - `tcpip_init()` with a "ready" callback.
- `app/FreeRTOSConfig.h` - backward-compatibility names on (the port uses `portTICK_RATE_MS`).
- `app/log/log.h` - `net` module.

## How to verify
```
.\build.ps1 clean all
```
The RAM line of the link output jumps by about 28 KB (lwIP pools). Console:
```
[I][net] tcpip thread up (lwIP 2.2.0)
```
The packet monitor from step 07 still runs; step 09 replaces it with the lwIP netif.

Mentee check: in the `.map` file find `memp_memory_PBUF_POOL_base` and `ram_heap` and
compare their sizes with `PBUF_POOL_SIZE * PBUF_POOL_BUFSIZE` and `MEM_SIZE`.
