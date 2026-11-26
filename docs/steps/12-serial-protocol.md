# Step 12 - Eco-Connect framed serial protocol task

## What we are solving
A second way into the gateway that does not need Ethernet: a framed link on USART1. It is
also the M1 fallback in the brief (sensor over UART "using Eco-Connect's own framing").

## Why two layers
- **Outer frame** (`$ BODY * CS CR LF`) only answers "did these bytes arrive intact, and
  where does a message start". Escaping (`/` + byte) keeps `$`, `*`, `/`, CR, LF out of the
  body, so an unescaped `$` always means "new frame" and the receiver resynchronises after
  noise or a truncated frame.
- **Inner packet** (`OP TYPE LL VALUE`) carries the meaning. It can be tested, logged and
  reused (the HTTP side calls the same handlers in step 13) without knowing about framing.
- **Checksum**: one's complement of the 8-bit sum of the inner packet bytes, after
  un-escaping. The earlier prototype summed the whole packet on receive but only the value
  on transmit, so its own frames failed; here encoder and decoder sum the same bytes and a
  host test builds 250 frames and checks that every one verifies.

## What changed
- `app/proto/serial_protocol.c/.h` - packet set/encode/decode, checksum, frame encode,
  streaming decoder (`proto_rx_feed`, one byte at a time, no buffering beyond one frame).
- `app/proto/proto_task.c/.h` - task: link bytes -> decoder -> handler -> response frame.
  Handles `PING` and `GET temp` for now; step 13 plugs in the registry.
- `drivers/uart_link.c/.h` - USART1 (PA9/PA10, 115200): RXNE interrupt into a static stream
  buffer, polled TX, overrun/error counters. `bsp/board.c/.h`: pins and MSP.
- `tests/test_serial_protocol.c` - 51 checks: codec, escaping, checksum symmetry, bad CS,
  bit flip, resync after noise, lowercase hex, overflow recovery, worst-case escaping.

## How to verify
```
make test
.\build.ps1 clean all
```
Host tests: `test_serial_protocol: 51 checks, 0 failed -> PASS`.

On the board, a USB-UART adapter on PA9/PA10 (3.3 V), terminal at 115200:
```
send   $PING N 00 *C3
reply  $RSP S 04 pong*..
send   $GET S 04 temp*52
reply  $RSP J .. {"node":1,"value":23.13,"timestamp":4004,"seq":2}*..
send   $GET S 04 temp*00
```
Console:
```
[I][proto] frame ok op=PING
[I][proto] frame ok op=GET
[W][proto] bad CS (rx 00 calc 52)
```
Mentee task: write a small PC script that builds frames with the same rules and fires
1000 random requests; compare the gateway's `bad_cs` counter with the frames you corrupted.
