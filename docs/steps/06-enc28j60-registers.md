# Step 06 - ENC28J60 register access, reset and revision check

## What we are solving
Talk to the ENC28J60 reliably: the right opcode, the right bank, the right number of bytes
back, and a reset sequence that leaves the chip in a known state.

## Why
Most ENC28J60 bugs in the wild are register-layer bugs that look like network bugs later:
- A banked register read in the wrong bank returns a different register, with no error.
- MAC and MII registers shift out a dummy byte first; reading them like ETH registers
  returns garbage.
- BFS/BFC are only defined for ETH registers.
- After the soft reset command, CLKRDY is not cleared (errata), so polling it right away
  says "ready" while the chip is still resetting. Wait at least 1 ms first.
- B1/B4 silicon needs SCK of 8 MHz or more (errata); we run 12.5 MHz.

## What changed
- `drivers/enc28j60/enc28j60_regs.h` - opcodes, register map with the bank and a MAC/MII flag
  encoded in each register constant, bit names, PHY registers, RX/TX status vector bits.
- `drivers/enc28j60/enc28j60.c/.h`
  - `select_bank()` with a cached bank; common registers 0x1B-0x1F skip the switch.
  - `op_read()` reads 2 or 3 bytes depending on the MAC/MII flag.
  - `enc_reg_write16()` writes L then H (needed for ERXRDPT in step 07).
  - `enc_phy_read/write()` through MIREGADR/MICMD with MISTAT.BUSY polling.
  - Reset: RESET pin pulse, SRC, wait 2 ms, then poll CLKRDY with a timeout.
  - Identity check: EREVID not 00h/FFh and PHID1 = 0083h, else "not present".
  - MAC set-up for half duplex, max frame 1518, MAC address, PHY LEDs.
- `bsp/board.c/.h` - `board_mac_address()`: 02:EC + 4 bytes from the device UID.
- `app/main.c` - init the chip, print revision and link state.

## How to verify
```
.\build.ps1 clean all
```
With the module wired (see README pin map):
```
[I][enc] rev B7
[I][enc] MAC 02:EC:xx:xx:xx:xx
[I][main] link up        (or "link down" without a cable)
```
Without the module: `[W][enc] not present (EREVID=0x.. PHID1=0x....)` and boot continues.

Mentee checks: capture the init sequence on the logic analyser (this is a milestone 1
evidence item); find the bank-select BFC/BFS pairs and the 3-byte MAC register reads.
