# Step 09 - ENC28J60 lwIP netif, static IP and DHCP

## What we are solving
Connect the driver (step 07) to the stack (step 08) so the board answers ARP and ping.

## Why
- The INT pin only wakes a thread; no SPI in interrupt context (the bus is shared and the
  mutex cannot be taken in an ISR).
- The RX thread runs above the tcpip thread so the 6.5 KB chip FIFO is emptied into the
  pbuf pool before it can overflow. It also wakes every 50 ms and polls EPKTCNT, so a lost
  INT edge costs latency, not a stuck receiver.
- Frames are copied straight into the pbuf chain (`rx_read` per pbuf) and out of it
  (`tx_write` per pbuf): no 1.5 KB bounce buffer.
- Link changes reach lwIP through `netif_set_link_up/down` under the core lock; with DHCP
  this restarts address acquisition after a cable is plugged in.

## What changed
- `middleware/lwip_port/ethernetif_enc28j60.c/.h` - netif init (MAC from the UID, MTU 1500,
  ARP/broadcast flags), `low_level_output`, `low_level_input`, `enc_rx` thread, counters.
- `app/net/net_init.c/.h` - `netif_add` from the tcpip thread, static address
  192.168.1.50/24 gw 192.168.1.1, status and link callbacks, `net_is_up()`, `net_ip_string()`.
- `Makefile` - `make DHCP=1` builds with DHCP instead of the static address.
- `app/main.c` - the step 07 packet monitor is gone; the netif owns the chip now.

## How to verify
```
.\build.ps1 clean all
```
Connect the board to a PC with the PC at 192.168.1.x/24:
```
[I][enc] rev B7
[I][net] tcpip thread up (lwIP 2.2.0)
[I][net] netif up 192.168.1.50
[I][net] link up
```
```
ping 192.168.1.50
Reply from 192.168.1.50: bytes=32 time=2ms TTL=255
```
With DHCP: `.\build.ps1 clean all DHCP=1` (always `clean` when changing the flag) and
look for the leased address in the `netif up` line.

Mentee check: Wireshark on the PC; find the ARP request/reply and the ICMP echo pair.
