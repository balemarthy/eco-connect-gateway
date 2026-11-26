/*
 * Independent watchdog (IWDG), clocked by the ~32 kHz LSI.
 * Once started it cannot be stopped; only a reset clears it.
 */
#ifndef IWDG_H
#define IWDG_H

#include <stdbool.h>
#include <stdint.h>

/* timeout_ms up to 8000 with the /64 prescaler used here. Also freezes the IWDG
   while the core is halted by a debugger. */
void iwdg_start(uint32_t timeout_ms);
void iwdg_feed(void);
/* true if the last reset came from the IWDG; clears all RCC reset flags. */
bool iwdg_caused_reset(void);

#endif /* IWDG_H */
