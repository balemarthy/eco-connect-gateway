# Step 07 - ENC28J60 RX/TX with buffer errata handled

## What we are solving
Move Ethernet frames in and out of the chip's 8 KB buffer without the classic ENC28J60
lock-ups: RX that silently stops, TX that never completes, a read pointer that corrupts
the FIFO.

## Why (errata from DS80349 and the buffer notes)
| Rule | Where in `enc28j60.c` |
|---|---|
| RX FIFO at 0000h, TX above it (RX 0000h-19FFh, TX 1A00h-1FFFh) | `RX_START`, `RX_END`, `TX_START` |
| ERXRDPT must be odd: next == ERXST ? ERXND : next - 1 | `rx_free()` |
| Write ERXRDPT L then H (only H latches) | `enc_reg_write16()` |
| PKTIF is unreliable: poll EPKTCNT | `enc28j60_service()`, `enc28j60_rx_begin()` |
| Free every frame with ECON2.PKTDEC (EPKTCNT stops at 255) | `rx_free()` |
| Pulse ECON1.TXRST before every TX; TXIF or TXERIF = done | `tx_once()` |
| Late collision: read TSV bit 29, retry | `enc28j60_tx_end()` |
| Never touch ERXST/ERXND with RXEN set | `rx_buffer_init()` callers clear RXEN first |
| Corrupt header (odd or out-of-range next pointer): RX reset | `rx_reset()` |

The INT pin (PC4, EXTI4, falling edge) never touches SPI in the ISR. It only notifies the
network task. `enc28j60_service()` clears EIE.INTIE while it reads EIR and sets it again at
the end, so a still-pending flag produces a new falling edge.

## What changed
- `drivers/enc28j60/enc28j60.c/.h` - buffer layout, receive filters (unicast + broadcast,
  CRC check), `rx_begin/rx_read/rx_end` (bus held for one frame), `tx_begin/tx_write/tx_end`
  (lets step 09 copy a pbuf chain without a bounce buffer), `enc28j60_service()`,
  statistics, `enc28j60_int_isr()`.
- `bsp/board.c/.h` - `board_enc_int_enable()`: PC4 EXTI falling edge, priority 6.
- `bsp/stm32f4xx_it.c` - `EXTI4_IRQHandler`.
- `app/main.c` - temporary packet monitor task: counts frames, sends a broadcast test frame
  (EtherType 88B5h) every 2 s, prints counters every 5 s. Replaced by lwIP in step 08/09.

## How to verify
```
.\build.ps1 clean all
```
With a cable to a switch or PC:
```
[I][enc] rev B7
[I][main] link up
[I][main] rx frames=12 tx=2 drop=0 ovf=0 txerr=0
```
`rx frames` grows with broadcast traffic on the LAN; `tx` grows by one every 2 s.
On the PC, Wireshark filter `eth.type == 0x88b5` shows the test frames.

Mentee check: unplug the cable and see `link down` from the LINKIF interrupt.
