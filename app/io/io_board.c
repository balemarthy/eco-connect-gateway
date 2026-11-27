/*
 * SPDX-License-Identifier: MIT
 *
 * Builds the board table (io_board) from IO_BOARD_DEVICES in the selected board header:
 * per peripheral one const descriptor, one static control block, the driver state and,
 * where the board reserves memory, the buffers and mode objects of the buffered modes.
 * Also the platform clock for polled waits.
 */
#include "io_board.h"

IO_BOARD_DEFINE_OBJECTS()

/* hardware description per entry */
#define IO_HW_DEF(id, path, type, drv, hwt, hw, privt, txm, rxm, txl, rxl, txms, rxms, txr, rxr, cs) \
    static const hwt hw_##id = hw;
IO_BOARD_DEVICES(IO_HW_DEF)
#undef IO_HW_DEF

/* driver state, buffers and mode objects; the arrays are zero-length (GNU C) where the
   board reserves nothing, so a peripheral without buffered modes pays nothing for them */
#define IO_BUF_DEF(id, path, type, drv, hwt, hw, privt, txm, rxm, txl, rxl, txms, rxms, txr, rxr, cs) \
    static privt   priv_##id;                                                                       \
    static uint8_t tx_##id[txr];                                                                    \
    static uint8_t rx_##id[rxr];                                                                    \
    static io_cq_t txq_##id[((txr) != 0U) ? 1 : 0];                                                 \
    static io_cq_t rxq_##id[((rxr) != 0U) ? 1 : 0];                                                 \
    static io_cb_t rxb_##id[((rxr) != 0U) ? 1 : 0];
IO_BOARD_DEVICES(IO_BUF_DEF)
#undef IO_BUF_DEF

#define IO_DESC_DEF(id, path, type, drv, hwt, hw, privt, txm, rxm, txl, rxl, txms, rxms, txr, rxr, cs) \
    { path, type, &drv, &hw_##id, &priv_##id, txm, rxm, txl, rxl, txms, rxms,                         \
      tx_##id, txr, rx_##id, rxr, txq_##id, rxq_##id, rxb_##id, cs },
static const io_desc_t devices[] = {
    IO_BOARD_DEVICES(IO_DESC_DEF)
};
#undef IO_DESC_DEF

static io_peripheral_t ctl[sizeof devices / sizeof devices[0]];

const io_board_t io_board = {
    IO_BOARD_NAME, devices, ctl, sizeof devices / sizeof devices[0]
};

uint32_t io_port_ms(void)
{
    return HAL_GetTick();
}
