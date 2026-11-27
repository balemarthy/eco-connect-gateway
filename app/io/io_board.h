/*
 * SPDX-License-Identifier: MIT
 *
 * Selects the board descriptor table for this build. Code that needs board names
 * (chip-select indices, the console path) includes this, not the board file itself.
 */
#ifndef IO_BOARD_H
#define IO_BOARD_H

#if defined(STM32F411xE)
#include "io_board_f411.h"
#else
#error "no IO board table for this target (add app/io/io_board_<board>.h)"
#endif

#endif /* IO_BOARD_H */
