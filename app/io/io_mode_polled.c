/*
 * SPDX-License-Identifier: MIT
 *
 * Polled mode: the calling task waits on a peripheral flag with a deadline.
 * Busy-waits (no yield), so it also works before the scheduler starts; the drivers use
 * it for byte-level handshakes that take microseconds (UART TXE/RXNE, SPI BSY).
 */
#include "io_mode.h"

const char *io_mode_name(io_mode_t m)
{
    static const char *const names[IO_MODE_COUNT] = {
        "polled", "zero copy", "char queue", "circular buffer"
    };
    return ((unsigned)m < IO_MODE_COUNT) ? names[m] : "?";
}

io_status_t io_polled_wait(io_ready_fn ready, void *ctx, uint32_t timeout_ms)
{
    if (ready == NULL) {
        return IO_ERR_PARAM;
    }
    const uint32_t t0 = io_port_ms();
    for (;;) {
        if (ready(ctx)) {
            return IO_OK;
        }
        if ((uint32_t)(io_port_ms() - t0) > timeout_ms) {
            /* one last look: the flag may have come up while the clock was read */
            return ready(ctx) ? IO_OK : IO_ERR_TIMEOUT;
        }
    }
}
