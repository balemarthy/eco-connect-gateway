# Step 18 - Integration, memory budget and architecture doc

## What we are solving
All parts are in. Now: does the whole thing fit with room to grow, and can a newcomer see
how the pieces talk to each other without reading every file?

## Why
- Milestone 1 of the brief asks for a RAM/ROM report with budget notes. Measuring from the
  `.map` and `arm-none-eabi-size` at the end of each feature keeps surprises small; the
  table in `docs/memory-budget.md` names the owner of every large buffer.
- One diagram of tasks, queues and interrupts answers most "who calls whom" questions and
  shows that the data path has no heap and no shared variables without a guard.

## What changed
- `docs/memory-budget.md` - flash 91 152 B of 512 KB (17 %), RAM 88 840 B of 128 KB (68 %),
  RAM by owner, FreeRTOS heap use, where the 39 KB headroom can go.
- `docs/architecture.md` - 4C map, Mermaid task/queue/ISR diagram, data path of one
  reading, priorities, layering rules.
- `README.md` - build options, interfaces, status table (done vs mentee task).
- `docs/steps/17-fsm-watchdog.md` - watched-task count at boot corrected.

## How to verify
```
make test
.\build.ps1 clean all
```
Expected tail:
```
Memory region         Used Size  Region Size  %age Used
           FLASH:       91152 B       512 KB     17.39%
             RAM:       88840 B       128 KB     67.78%
   text    data     bss     dec     hex filename
  91016     128   88720  179864   2be98 build/eco-connect-gateway.elf
```
Full boot on the board (cable in, no W25Q driver yet):
```
Eco-Connect boot
[I][main] boot
[I][main] SYSCLK=100000000 src=HSE
[I][main] registry: 6 commands
[I][spi] SPI2 ready, 12.5 MHz
[I][hb] scheduler started
[I][net] tcpip thread up (lwIP 2.2.0)
[I][enc] rev B7
[I][http] listening :80
[W][fs] no media, logging off
[I][net] netif up 192.168.1.50
[I][app] seq=1 node=1 t=23.00C
[I][fsm] IDLE -> SAMPLING (reading_ok)
[I][fsm] SAMPLING -> CONNECTED (link_up)
```
(order of lines from different tasks can vary)

Mentee tasks left: I2C sensor driver (step 10), W25Q data path (step 15), on-board
evidence: logic analyser capture of the ENC28J60 init, Wireshark capture of an HTTP
request/response, ping, stack high-water marks, demo video.
