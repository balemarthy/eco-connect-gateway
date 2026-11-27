# Demo: TCP echo (`make DEMO=tcp_echo`)

Echo server on TCP port `TCP_ECHO_PORT` (default 7, `app/config/app_config.h`). Everything the
client sends comes back unchanged. Shows the plain lwIP netconn pattern the HTTP server also uses,
without HTTP on top.

| Item | Value |
|---|---|
| Source | `app/demos/tcp_echo.c` (own code; the reference `tcp-echo` archive has no licence, only the idea - netconn bind/listen/accept/recv/write - is the same) |
| API | lwIP netconn (`LWIP_NETCONN 1`, `LWIP_SOCKET 0` in `middleware/lwip_port/lwipopts.h`) |
| Task | `echo`, 1024 words static stack, priority idle+2, supervisor window 8 s |
| Clients | one at a time (pools: 4 TCP PCBs, 2 listeners = HTTP :80 + echo) |
| Limits | closes after `TCP_ECHO_RECV_TIMEOUT_MS` (10 s) idle, after `TCP_ECHO_MAX_BYTES` (64 KB), or when a write blocks 2 s (peer not reading) |
| Cost (measured) | +628 B flash, +4,200 B RAM over `DEMO=none` (`logs/size-tcp_echo.txt` in the job folder) |

## Flow
```
echo_task: wait net_stack_ready -> netconn_new/bind/listen(:7), recv timeout 1 s
  loop: netconn_accept (1 s) -> check in
        serve(): netconn_recv (1 s) -> netconn_write(NETCONN_COPY) for every netbuf part
                 -> close / delete
```
Every blocking call has a timeout of 1-2 s and the loop calls `sup_checkin()` after each, so a hung
connection cannot starve the watchdog supervisor.

## Try it (on the board, not run in this job)
```
make DEMO=tcp_echo            # or .\build.ps1 -Demo tcp_echo
ncat 192.168.1.50 7           # type a line, the same line comes back
```
Console: `[I][demo] echo: listening :7`, then `[I][demo] echo: client 1`,
`[I][demo] client done: 12 bytes (peer closed)`.
