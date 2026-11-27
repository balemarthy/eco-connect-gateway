/*
 * SPDX-License-Identifier: MIT
 *
 * Host tests: IO layer core and transfer modes (app/io/io_core.c, io_mode_*.c) on a test
 * board table with fake drivers, against the FreeRTOS stand-ins in tests/stubs/.
 * Covered: path rules and lookup, open by name (once per peripheral, failing drivers,
 * bad board defaults), read/write dispatch and status, ioctl validation per peripheral
 * type, mode selection rules and static-memory limits, the zero-copy write mutex
 * (bus lock and ISR-released), circular buffer, character queue, polled wait.
 */
#include <stdint.h>
#include <string.h>

#include "io_driver.h"
#include "test_util.h"

/* ------------------------------------------------------------------ platform clock */
static uint32_t host_ms;
static uint32_t host_ms_step = 1U;            /* every io_port_ms() call advances the clock */

uint32_t io_port_ms(void)
{
    uint32_t t = host_ms;
    host_ms += host_ms_step;
    return t;
}

/* ------------------------------------------------------------------ fake drivers */
static unsigned    fake_opens[8];
static io_status_t fake_open_result = IO_OK;
static io_status_t fake_rw_status = IO_OK;    /* what the next read/write reports */
static size_t      fake_rw_count = SIZE_MAX;  /* bytes the next read/write moves (SIZE_MAX: all) */
static uint8_t     fake_wr[64];
static size_t      fake_wr_len;
static uint32_t    fake_last_ioctl;
static unsigned    fake_mode_calls;
static bool        fake_mode_last_tx;
static io_mode_t   fake_mode_seen;

static unsigned entry_of(const io_peripheral_t *p)
{
    return (unsigned)(p - io_board.ctl);
}

static io_status_t fake_open(io_peripheral_t *p)
{
    fake_opens[entry_of(p)]++;
    return (strcmp(p->desc->path, "/BAD1/") == 0) ? IO_ERR_NOT_READY : fake_open_result;
}

static size_t moved(size_t len)
{
    return (fake_rw_count == SIZE_MAX || fake_rw_count > len) ? len : fake_rw_count;
}

static size_t fake_write(io_peripheral_t *p, const uint8_t *buf, size_t len)
{
    size_t n = moved(len);
    size_t keep = (n < sizeof fake_wr) ? n : sizeof fake_wr;
    memcpy(fake_wr, buf, keep);
    fake_wr_len = n;
    p->last = fake_rw_status;
    return n;
}

static size_t fake_read(io_peripheral_t *p, uint8_t *buf, size_t len)
{
    size_t n = moved(len);
    for (size_t i = 0U; i < n; i++) {
        buf[i] = (uint8_t)(0xA0U + i);
    }
    p->last = fake_rw_status;
    return n;
}

static io_status_t fake_ioctl(io_peripheral_t *p, uint32_t request, void *value)
{
    (void)p;
    (void)value;
    fake_last_ioctl = request;
    return (request == IO_IOCTL_SET_SPEED || request == IO_IOCTL_RELEASE_WRITE_MUTEX) ? IO_OK : IO_ERR_UNSUPPORTED;
}

static void fake_mode_changed(io_peripheral_t *p, bool tx)
{
    fake_mode_calls++;
    fake_mode_last_tx = tx;
    fake_mode_seen = tx ? p->tx_mode : p->rx_mode;
}

static const io_driver_t fake_drv = { fake_open, fake_write, fake_read, fake_ioctl, fake_mode_changed };

/* ------------------------------------------------------------------ test board */
enum { E_UART1, E_UART2, E_SPI1, E_I2C1, E_BAD1, E_BAD2, E_IDLE, E_COUNT };

static uint8_t u1_tx[32], u1_rx[64], u2_tx[16], bad2_rx[64], idle_rx[64];
static io_cq_t u1_txq[1], u1_rxq[1], u2_txq[1], bad2_rxq[1], idle_rxq[1];
static io_cb_t u1_rxb[1], bad2_rxb[1], idle_rxb[1];

static const io_desc_t devices[E_COUNT] = {
    /* path, type, drv, hw, priv, tx mode, rx mode, tx len, rx len, tx ms, rx ms,
       tx store, tx reserve, rx store, rx reserve, txq, rxq, rxb, cs */
    { "/UART1/", IO_TYPE_UART, &fake_drv, NULL, NULL, IO_MODE_POLLED, IO_MODE_CIRC_BUF, 0U, 64U, 100U, 1000U,
      u1_tx, sizeof u1_tx, u1_rx, sizeof u1_rx, u1_txq, u1_rxq, u1_rxb, 0U },
    { "/UART2/", IO_TYPE_UART, &fake_drv, NULL, NULL, IO_MODE_CHAR_QUEUE, IO_MODE_POLLED, 16U, 0U, 100U, 100U,
      u2_tx, sizeof u2_tx, NULL, 0U, u2_txq, NULL, NULL, 0U },
    { "/SPI1/", IO_TYPE_SPI, &fake_drv, NULL, NULL, IO_MODE_ZERO_COPY, IO_MODE_POLLED, 0U, 0U, 50U, 50U,
      NULL, 0U, NULL, 0U, NULL, NULL, NULL, 2U },
    { "/I2C1/", IO_TYPE_I2C, &fake_drv, NULL, NULL, IO_MODE_POLLED, IO_MODE_POLLED, 0U, 0U, 100U, 100U,
      NULL, 0U, NULL, 0U, NULL, NULL, NULL, 0U },
    { "/BAD1/", IO_TYPE_UART, &fake_drv, NULL, NULL, IO_MODE_POLLED, IO_MODE_POLLED, 0U, 0U, 100U, 100U,
      NULL, 0U, NULL, 0U, NULL, NULL, NULL, 0U },
    /* default ring of 48 bytes: not a power of two, so open must fail */
    { "/BAD2/", IO_TYPE_UART, &fake_drv, NULL, NULL, IO_MODE_POLLED, IO_MODE_CIRC_BUF, 0U, 48U, 100U, 100U,
      NULL, 0U, bad2_rx, sizeof bad2_rx, NULL, bad2_rxq, bad2_rxb, 0U },
    { "/IDLE/", IO_TYPE_UART, &fake_drv, NULL, NULL, IO_MODE_POLLED, IO_MODE_POLLED, 0U, 0U, 100U, 100U,
      NULL, 0U, idle_rx, sizeof idle_rx, NULL, idle_rxq, idle_rxb, 0U },
};
static io_peripheral_t ctl[E_COUNT];
const io_board_t io_board = { "host", devices, ctl, E_COUNT };

#define TASK_A ((void *)0x1000)
#define TASK_B ((void *)0x2000)

static void reset_stubs(void)
{
    stub_tick = 0U;
    stub_sched_running = 1;
    stub_current = TASK_A;
    stub_on_block = NULL;
    stub_block_calls = 0U;
    fake_rw_status = IO_OK;
    fake_rw_count = SIZE_MAX;
}

/* ------------------------------------------------------------------ path and lookup */
static void test_paths(void)
{
    CHECK(io_path_valid("/UART1/"));
    CHECK(io_path_valid("/I2C1/"));
    CHECK(io_path_valid("/A/"));
    CHECK(io_path_valid("/SPI_2/"));
    CHECK(!io_path_valid(NULL));
    CHECK(!io_path_valid(""));
    CHECK(!io_path_valid("/"));
    CHECK(!io_path_valid("//"));
    CHECK(!io_path_valid("UART1"));
    CHECK(!io_path_valid("/UART1"));
    CHECK(!io_path_valid("UART1/"));
    CHECK(!io_path_valid("/uart1/"));               /* names are upper case */
    CHECK(!io_path_valid("/UA RT/"));
    CHECK(!io_path_valid("/UART/1/"));
    CHECK(!io_path_valid("/ABCDEFGHIJKLMNOP/"));    /* longer than 16 characters */

    CHECK_EQ_INT(io_find(&io_board, "/UART1/"), E_UART1);
    CHECK_EQ_INT(io_find(&io_board, "/I2C1/"), E_I2C1);
    CHECK_EQ_INT(io_find(&io_board, "/IDLE/"), E_IDLE);
    CHECK_EQ_INT(io_find(&io_board, "/UART9/"), -1);
    CHECK_EQ_INT(io_find(&io_board, "/UART1"), -1);
    CHECK_EQ_INT(io_find(NULL, "/UART1/"), -1);
}

/* ------------------------------------------------------------------ open */
static void test_open(void)
{
    reset_stubs();
    io_handle_t u1 = io_open("/UART1/", 0U);
    CHECK(u1 != NULL);
    CHECK(u1 == &io_board.ctl[E_UART1]);
    CHECK_EQ_INT(fake_opens[E_UART1], 1);
    CHECK_EQ_INT(u1->tx_mode, IO_MODE_POLLED);
    CHECK_EQ_INT(u1->rx_mode, IO_MODE_CIRC_BUF);   /* board default applied */
    CHECK_EQ_INT(u1->rxb->size, 64);
    CHECK_EQ_INT(u1->rx_timeout_ms, 1000);
    CHECK_EQ_INT(u1->tx_timeout_ms, 100);
    CHECK(io_open("/UART1/", 0U) == u1);            /* same control block, no second init */
    CHECK_EQ_INT(fake_opens[E_UART1], 1);
    CHECK_EQ_STR(io_path(u1), "/UART1/");
    CHECK_EQ_STR(io_path(NULL), "?");

    CHECK(io_open("/UART1/", 1U) == NULL);          /* flags are reserved */
    CHECK(io_open("/UART9/", 0U) == NULL);
    CHECK(io_open(NULL, 0U) == NULL);
    CHECK(io_open("/uart1/", 0U) == NULL);

    CHECK(io_open("/BAD1/", 0U) == NULL);           /* driver open fails */
    CHECK(io_open("/BAD1/", 0U) == NULL);
    CHECK_EQ_INT(fake_opens[E_BAD1], 2);            /* tried again, not marked open */
    CHECK(!io_board.ctl[E_BAD1].is_open);
    CHECK(io_open("/BAD2/", 0U) == NULL);           /* default ring size invalid */
    CHECK(!io_board.ctl[E_BAD2].is_open);

    io_handle_t u2 = io_open("/UART2/", 0U);
    CHECK(u2 != NULL);
    CHECK_EQ_INT(u2->tx_mode, IO_MODE_CHAR_QUEUE);
    CHECK_EQ_INT(u2->txq->len, 16);
    io_handle_t spi = io_open("/SPI1/", 0U);
    CHECK(spi != NULL);
    CHECK_EQ_INT(spi->tx_mode, IO_MODE_ZERO_COPY);
    CHECK_EQ_INT(spi->cs, -1);
    CHECK(io_open("/I2C1/", 0U) != NULL);

    /* before the scheduler: same result */
    stub_sched_running = 0;
    CHECK(io_open("/UART1/", 0U) == u1);
    stub_sched_running = 1;
}

/* ------------------------------------------------------------------ read / write */
static void test_read_write(void)
{
    reset_stubs();
    io_handle_t u1 = io_open("/UART1/", 0U);
    io_handle_t i2c = io_open("/I2C1/", 0U);
    io_status_t st = IO_ERR_IO;
    uint8_t buf[8];
    const uint8_t msg[5] = { 'h', 'e', 'l', 'l', 'o' };

    CHECK_EQ_INT(io_write(NULL, msg, 5U), 0);
    CHECK_EQ_INT(io_read(NULL, buf, 5U), 0);

    io_handle_t idle = &io_board.ctl[E_IDLE];      /* never opened */
    CHECK_EQ_INT(io_write(idle, msg, 5U), 0);
    CHECK_EQ_INT(idle->last, IO_ERR_NOT_READY);
    CHECK_EQ_INT(io_read(idle, buf, 1U), 0);
    CHECK_EQ_INT(io_ioctl(idle, IO_IOCTL_SET_TX_TIMEOUT, IO_VAL(5U)), IO_ERR_NOT_READY);

    CHECK_EQ_INT(io_write(i2c, NULL, 3U), 0);
    CHECK_EQ_INT(io_ioctl(i2c, IO_IOCTL_GET_LAST_STATUS, &st), IO_OK);
    CHECK_EQ_INT(st, IO_ERR_PARAM);
    CHECK_EQ_INT(io_write(i2c, msg, 0U), 0);
    (void)io_ioctl(i2c, IO_IOCTL_GET_LAST_STATUS, &st);
    CHECK_EQ_INT(st, IO_OK);

    CHECK_EQ_INT(io_write(i2c, msg, sizeof msg), 5);
    CHECK_EQ_INT(fake_wr_len, 5);
    CHECK(memcmp(fake_wr, msg, 5U) == 0);
    CHECK_EQ_INT(io_read(i2c, buf, 3U), 3);
    CHECK_EQ_INT(buf[2], 0xA2);

    fake_rw_status = IO_ERR_TIMEOUT;
    fake_rw_count = 2U;
    CHECK_EQ_INT(io_write(i2c, msg, sizeof msg), 2);
    (void)io_ioctl(i2c, IO_IOCTL_GET_LAST_STATUS, &st);
    CHECK_EQ_INT(st, IO_ERR_TIMEOUT);
    fake_rw_status = IO_ERR_IO;
    CHECK_EQ_INT(io_read(i2c, buf, 4U), 2);
    (void)io_ioctl(i2c, IO_IOCTL_GET_LAST_STATUS, &st);
    CHECK_EQ_INT(st, IO_ERR_IO);

    io_stats_t s;
    CHECK_EQ_INT(io_ioctl(i2c, IO_IOCTL_GET_STATS, &s), IO_OK);
    CHECK_EQ_INT(s.tx_bytes, 7);
    CHECK_EQ_INT(s.rx_bytes, 5);
    CHECK_EQ_INT(s.timeouts, 1);
    CHECK_EQ_INT(io_ioctl(i2c, IO_IOCTL_GET_STATS, NULL), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl(NULL, IO_IOCTL_GET_STATS, &s), IO_ERR_PARAM);

    /* the UART1 default ring exists and is the board's static memory; reading it is the
       driver's job (io_uart.c), the core passes io_read straight to the driver */
    reset_stubs();
    BaseType_t w;
    CHECK(u1->rxb == devices[E_UART1].rxb);
    CHECK(u1->rxb->buf == devices[E_UART1].rx_storage);
    CHECK(io_cb_put_from_isr(u1->rxb, 0x55U, &w));
    CHECK_EQ_INT(io_cb_read(u1->rxb, buf, sizeof buf, 0U, &st), 1);
    CHECK_EQ_INT(buf[0], 0x55);
    CHECK_EQ_INT(io_read(u1, buf, 2U), 2);
    CHECK_EQ_INT(buf[0], 0xA0);                          /* from the fake driver */
}

/* ------------------------------------------------------------------ ioctl validation */
static void test_ioctl_check(void)
{
    const io_desc_t *u1 = &devices[E_UART1];
    const io_desc_t *u2 = &devices[E_UART2];
    const io_desc_t *spi = &devices[E_SPI1];
    const io_desc_t *i2c = &devices[E_I2C1];
    uint8_t reg = 0U, rd[2];
    io_i2c_xfer_t x = { &reg, 1U, rd, 2U };
    io_spi_xfer_t sx = { NULL, NULL, 0U };

    CHECK_EQ_INT(io_ioctl_check(NULL, IO_IOCTL_USE_POLLED_TX, NULL), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, 9999U, NULL), IO_ERR_UNSUPPORTED);
    CHECK_EQ_INT(io_ioctl_check(u1, 0U, NULL), IO_ERR_UNSUPPORTED);

    /* modes every type has */
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_USE_POLLED_TX, NULL), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_USE_POLLED_RX, NULL), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_USE_ZERO_COPY_TX, NULL), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_ZERO_COPY_TX, NULL), IO_OK);

    /* character queue: UART only, length > 0 and within the reserve */
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(32U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(33U)), IO_ERR_NO_MEMORY);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(0U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CHAR_QUEUE_RX, IO_VAL(64U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u2, IO_IOCTL_USE_CHAR_QUEUE_RX, IO_VAL(1U)), IO_ERR_NO_MEMORY);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(8U)), IO_ERR_UNSUPPORTED);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_USE_CHAR_QUEUE_RX, IO_VAL(8U)), IO_ERR_UNSUPPORTED);

    /* circular buffer: UART RX, power of two >= 16, within the reserve */
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(64U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(16U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(8U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(48U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(128U)), IO_ERR_NO_MEMORY);
    CHECK_EQ_INT(io_ioctl_check(u2, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(16U)), IO_ERR_NO_MEMORY);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(64U)), IO_ERR_UNSUPPORTED);

    /* mutex and timeouts: always valid (the mode is checked when it runs) */
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(10U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE, IO_VAL(1U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SET_TX_TIMEOUT, IO_VAL(0U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SET_RX_TIMEOUT, IO_VAL(60000U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_CLEAR_RX_BUFFER, NULL), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_CLEAR_RX_BUFFER, NULL), IO_ERR_UNSUPPORTED);
    CHECK_EQ_INT(io_ioctl_check(u2, IO_IOCTL_WAIT_TX_EMPTY, IO_VAL(10U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_WAIT_TX_EMPTY, IO_VAL(10U)), IO_ERR_UNSUPPORTED);

    /* speed per type */
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_SPEED, IO_VAL(115200U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_SPEED, IO_VAL(IO_UART_BAUD_MIN)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_SPEED, IO_VAL(IO_UART_BAUD_MAX)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_SPEED, IO_VAL(300U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_SPEED, IO_VAL(IO_UART_BAUD_MAX + 1U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SET_SPEED, IO_VAL(12500000U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SET_SPEED, IO_VAL(50000U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SET_SPEED, IO_VAL(60000000U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_SET_SPEED, IO_VAL(400000U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_SET_SPEED, IO_VAL(1000000U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_SET_SPEED, IO_VAL(9999U)), IO_ERR_PARAM);

    /* interrupt priority: UART only (SPI/I2C are polled), 5..15 */
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_INTERRUPT_PRIORITY, IO_VAL(5U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_INTERRUPT_PRIORITY, IO_VAL(15U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_INTERRUPT_PRIORITY, IO_VAL(4U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SET_INTERRUPT_PRIORITY, IO_VAL(16U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SET_INTERRUPT_PRIORITY, IO_VAL(6U)), IO_ERR_UNSUPPORTED);

    /* SPI requests */
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SPI_SET_MODE, IO_VAL(3U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SPI_SET_MODE, IO_VAL(4U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SPI_SELECT, IO_VAL(1U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SPI_SELECT, IO_VAL(2U)), IO_ERR_PARAM);   /* 2 chip selects */
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SPI_DESELECT, NULL), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SPI_TRANSFER, &sx), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_SPI_TRANSFER, NULL), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_SPI_SELECT, IO_VAL(0U)), IO_ERR_UNSUPPORTED);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_SPI_TRANSFER, &sx), IO_ERR_UNSUPPORTED);

    /* I2C requests */
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x48U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x08U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x77U)), IO_OK);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x07U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x78U)), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(spi, IO_IOCTL_I2C_SET_SLAVE_ADDRESS, IO_VAL(0x48U)), IO_ERR_UNSUPPORTED);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_WRITE_READ, &x), IO_OK);
    x.wr_len = 2U;
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_WRITE_READ, &x), IO_OK);
    x.wr_len = 3U;
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_WRITE_READ, &x), IO_ERR_PARAM);
    x.wr_len = 1U;
    x.rd_len = 0U;
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_WRITE_READ, &x), IO_ERR_PARAM);
    x.rd_len = 2U;
    x.rd = NULL;
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_WRITE_READ, &x), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(i2c, IO_IOCTL_I2C_WRITE_READ, NULL), IO_ERR_PARAM);

    /* information */
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_GET_LAST_STATUS, NULL), IO_ERR_PARAM);
    CHECK_EQ_INT(io_ioctl_check(u1, IO_IOCTL_GET_LAST_STATUS, &reg), IO_OK);

    CHECK(io_mode_allowed(IO_TYPE_UART, false, IO_MODE_CIRC_BUF));
    CHECK(!io_mode_allowed(IO_TYPE_UART, true, IO_MODE_CIRC_BUF));
    CHECK(!io_mode_allowed(IO_TYPE_UART, false, IO_MODE_ZERO_COPY));
    CHECK(io_mode_allowed(IO_TYPE_SPI, true, IO_MODE_ZERO_COPY));
    CHECK(!io_mode_allowed(IO_TYPE_COUNT, true, IO_MODE_POLLED));
    CHECK(!io_mode_allowed(IO_TYPE_UART, true, IO_MODE_COUNT));
}

/* ------------------------------------------------------------------ modes through io_ioctl */
static void test_modes(void)
{
    reset_stubs();
    io_handle_t u1 = io_open("/UART1/", 0U);
    io_handle_t spi = io_open("/SPI1/", 0U);

    fake_mode_calls = 0U;
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(32U)), IO_OK);
    CHECK_EQ_INT(u1->tx_mode, IO_MODE_CHAR_QUEUE);
    CHECK_EQ_INT(fake_mode_calls, 2);                  /* off (polled) while rebuilt, then on */
    CHECK(fake_mode_last_tx);
    CHECK_EQ_INT(fake_mode_seen, IO_MODE_CHAR_QUEUE);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(16U)), IO_ERR_STATE);   /* length fixed */
    CHECK_EQ_INT(u1->tx_mode, IO_MODE_POLLED);         /* refused selection leaves it polled */
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(32U)), IO_OK);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(64U)), IO_ERR_NO_MEMORY);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_POLLED_TX, NULL), IO_OK);
    CHECK_EQ_INT(u1->tx_mode, IO_MODE_POLLED);

    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(32U)), IO_OK);
    CHECK_EQ_INT(u1->rxb->size, 32);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_CHAR_QUEUE_RX, IO_VAL(8U)), IO_OK);
    CHECK_EQ_INT(u1->rx_mode, IO_MODE_CHAR_QUEUE);
    CHECK(!fake_mode_last_tx);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_POLLED_RX, NULL), IO_OK);
    CHECK_EQ_INT(u1->rx_mode, IO_MODE_POLLED);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_USE_CIRCULAR_BUFFER_RX, IO_VAL(64U)), IO_OK);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_CLEAR_RX_BUFFER, NULL), IO_OK);

    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(4U)), IO_ERR_UNSUPPORTED);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_SET_TX_TIMEOUT, IO_VAL(7U)), IO_OK);
    CHECK_EQ_INT(spi->tx_timeout_ms, 7);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_SET_RX_TIMEOUT, IO_VAL(9U)), IO_OK);
    CHECK_EQ_INT(spi->rx_timeout_ms, 9);

    /* driver-specific requests reach the driver */
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_SET_SPEED, IO_VAL(1000000U)), IO_OK);
    CHECK_EQ_INT(fake_last_ioctl, IO_IOCTL_SET_SPEED);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_SPI_SELECT, IO_VAL(1U)), IO_ERR_UNSUPPORTED);
    CHECK_EQ_INT(fake_last_ioctl, IO_IOCTL_SPI_SELECT);
    fake_last_ioctl = 0U;
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_SPI_SELECT, IO_VAL(5U)), IO_ERR_PARAM);
    CHECK_EQ_INT(fake_last_ioctl, 0);                   /* refused before the driver */

    /* mutex requests outside zero-copy mode */
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(1U)), IO_ERR_STATE);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_ERR_STATE);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE, IO_VAL(1U)), IO_OK);
    CHECK_EQ_INT(io_ioctl(u1, IO_IOCTL_WAIT_TX_EMPTY, IO_VAL(1U)), IO_OK);

    CHECK_EQ_STR(io_mode_name(IO_MODE_CIRC_BUF), "circular buffer");
    CHECK_EQ_STR(io_mode_name(IO_MODE_COUNT), "?");
    CHECK_EQ_STR(io_status_str(IO_ERR_NOT_OWNER), "not owner");
    CHECK_EQ_STR(io_status_str(IO_STATUS_COUNT), "?");
}

/* ------------------------------------------------------------------ zero copy: bus lock */
static void test_zero_copy_bus(void)
{
    reset_stubs();
    io_handle_t spi = io_open("/SPI1/", 0U);
    const uint8_t cmd[2] = { 0x9FU, 0x00U };

    CHECK_EQ_INT(io_write(spi, cmd, 2U), 0);              /* no mutex */
    CHECK_EQ_INT(spi->last, IO_ERR_NOT_OWNER);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_ERR_NOT_OWNER);

    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(10U)), IO_OK);
    CHECK(io_zc_held(&spi->zc));
    CHECK(io_zc_caller_holds(&spi->zc));
    CHECK_EQ_INT(io_write(spi, cmd, 2U), 2);
    CHECK_EQ_INT(io_write(spi, cmd, 1U), 1);              /* keeps the lock across transfers */
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(10U)), IO_ERR_STATE);   /* not recursive */
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_USE_POLLED_TX, NULL), IO_ERR_STATE);              /* held */

    stub_current = TASK_B;                                  /* another task */
    CHECK(!io_zc_caller_holds(&spi->zc));
    CHECK_EQ_INT(io_write(spi, cmd, 2U), 0);
    CHECK_EQ_INT(spi->last, IO_ERR_NOT_OWNER);
    TickType_t t0 = stub_tick;
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(25U)), IO_ERR_TIMEOUT);
    CHECK_EQ_INT(stub_tick - t0, 25);                      /* waited the full timeout */
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_ERR_NOT_OWNER);

    stub_current = TASK_A;
    fake_last_ioctl = 0U;
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_OK);
    CHECK_EQ_INT(fake_last_ioctl, IO_IOCTL_RELEASE_WRITE_MUTEX);   /* driver saw it first */
    CHECK(!io_zc_held(&spi->zc));
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_ERR_NOT_OWNER);

    stub_current = TASK_B;
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(25U)), IO_OK);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_OK);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_WAIT_PREVIOUS_WRITE_COMPLETE, IO_VAL(5U)), IO_OK);
    CHECK(!io_zc_held(&spi->zc));

    /* before the scheduler: one thread, no kernel object involved */
    stub_sched_running = 0;
    CHECK(io_zc_caller_holds(&spi->zc));
    CHECK_EQ_INT(io_write(spi, cmd, 2U), 2);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(1U)), IO_OK);
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL), IO_OK);
    CHECK(!io_zc_held(&spi->zc));
    stub_sched_running = 1;

    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_USE_POLLED_TX, NULL), IO_OK);
    CHECK_EQ_INT(io_write(spi, cmd, 2U), 2);                /* polled: no mutex needed */
    CHECK_EQ_INT(io_ioctl(spi, IO_IOCTL_USE_ZERO_COPY_TX, NULL), IO_OK);
    CHECK_EQ_INT(io_write(spi, cmd, 2U), 0);
}

/* ------------------------------------------------------------------ zero copy: ISR-released */
static io_zc_t isr_zc;

static void isr_finishes_tx(void)
{
    BaseType_t w;
    io_zc_release_from_isr(&isr_zc, &w);
}

static void test_zero_copy_isr(void)
{
    reset_stubs();
    memset(&isr_zc, 0, sizeof isr_zc);
    CHECK_EQ_INT(io_zc_obtain(&isr_zc, 1U), IO_ERR_STATE);  /* not created */
    CHECK_EQ_INT(io_zc_init(&isr_zc, true), IO_OK);
    CHECK_EQ_INT(io_zc_init(&isr_zc, true), IO_OK);         /* second init keeps it */
    CHECK_EQ_INT(io_zc_init(&isr_zc, false), IO_ERR_STATE); /* kind is fixed */
    CHECK_EQ_INT(io_zc_init(NULL, true), IO_ERR_PARAM);

    CHECK_EQ_INT(io_zc_obtain(&isr_zc, 10U), IO_OK);        /* free at start */
    io_zc_pass_to_isr(&isr_zc);                              /* the write is in flight */
    CHECK(io_zc_held(&isr_zc));
    CHECK(!io_zc_caller_holds(&isr_zc));
    CHECK_EQ_INT(io_zc_release(&isr_zc), IO_ERR_NOT_OWNER);  /* the ISR owns it now */

    /* the next writer waits; the "ISR" ends the transfer while it waits */
    stub_on_block = isr_finishes_tx;
    CHECK_EQ_INT(io_zc_wait_complete(&isr_zc, 50U), IO_OK);
    stub_on_block = NULL;
    CHECK(!io_zc_held(&isr_zc));
    CHECK_EQ_INT(io_zc_obtain(&isr_zc, 10U), IO_OK);
    CHECK_EQ_INT(io_zc_release(&isr_zc), IO_OK);

    /* an ISR release when nothing was handed over changes nothing */
    BaseType_t w;
    io_zc_release_from_isr(&isr_zc, &w);
    CHECK(!io_zc_held(&isr_zc));
    CHECK_EQ_INT(io_zc_obtain(&isr_zc, 0U), IO_OK);
    io_zc_release_from_isr(&isr_zc, &w);                     /* holder is a task: ignored */
    CHECK(io_zc_held(&isr_zc));
    CHECK_EQ_INT(io_zc_release(&isr_zc), IO_OK);
}

/* ------------------------------------------------------------------ circular buffer */
static io_cb_t cb;
static uint8_t cb_mem[64];

static void isr_delivers_two(void)
{
    BaseType_t w;
    (void)io_cb_put_from_isr(&cb, 'O', &w);
    (void)io_cb_put_from_isr(&cb, 'K', &w);
}

static void test_circ_buf(void)
{
    reset_stubs();
    io_status_t st;
    uint8_t out[80];
    BaseType_t w;

    CHECK(io_cb_size_ok(16U, 64U));
    CHECK(io_cb_size_ok(64U, 64U));
    CHECK(!io_cb_size_ok(128U, 64U));
    CHECK(!io_cb_size_ok(8U, 64U));
    CHECK(!io_cb_size_ok(0U, 64U));
    CHECK(!io_cb_size_ok(24U, 64U));
    CHECK_EQ_INT(io_cb_init(&cb, NULL, 64U), IO_ERR_PARAM);
    CHECK_EQ_INT(io_cb_init(NULL, cb_mem, 64U), IO_ERR_PARAM);
    CHECK_EQ_INT(io_cb_init(&cb, cb_mem, 40U), IO_ERR_PARAM);
    CHECK_EQ_INT(io_cb_init(&cb, cb_mem, 16U), IO_OK);
    CHECK_EQ_INT(io_cb_count(&cb), 0);

    /* all 16 bytes usable, the 17th is an overrun */
    for (unsigned i = 0U; i < 16U; i++) {
        CHECK(io_cb_put_from_isr(&cb, (uint8_t)i, &w));
    }
    CHECK(!io_cb_put_from_isr(&cb, 0xEEU, &w));
    CHECK_EQ_INT(cb.overruns, 1);
    CHECK_EQ_INT(io_cb_count(&cb), 16);

    /* read returns what is there, up to len, oldest first */
    CHECK_EQ_INT(io_cb_read(&cb, out, 5U, 100U, &st), 5);
    CHECK_EQ_INT(st, IO_OK);
    CHECK_EQ_INT(out[0], 0);
    CHECK_EQ_INT(out[4], 4);
    CHECK_EQ_INT(io_cb_read(&cb, out, sizeof out, 100U, &st), 11);
    CHECK_EQ_INT(out[10], 15);
    CHECK_EQ_INT(stub_tick, 0);                              /* never waited */

    /* empty: waits the timeout, then 0 / timeout */
    CHECK_EQ_INT(io_cb_read(&cb, out, 4U, 30U, &st), 0);
    CHECK_EQ_INT(st, IO_ERR_TIMEOUT);
    CHECK_EQ_INT(stub_tick, 30);
    CHECK_EQ_INT(io_cb_read(&cb, out, 4U, 0U, &st), 0);      /* zero timeout: no wait */
    CHECK_EQ_INT(st, IO_ERR_TIMEOUT);
    CHECK_EQ_INT(stub_tick, 30);
    CHECK_EQ_INT(io_cb_read(&cb, out, 0U, 30U, &st), 0);
    CHECK_EQ_INT(st, IO_OK);

    /* data arriving while the reader sleeps wakes it */
    stub_on_block = isr_delivers_two;
    CHECK_EQ_INT(io_cb_read(&cb, out, 8U, 1000U, &st), 2);
    stub_on_block = NULL;
    CHECK_EQ_INT(st, IO_OK);
    CHECK_EQ_INT(out[0], 'O');
    CHECK_EQ_INT(out[1], 'K');
    CHECK_EQ_INT(stub_tick, 30);

    /* free-running indices across the 32-bit wrap */
    CHECK_EQ_INT(io_cb_init(&cb, cb_mem, 64U), IO_OK);
    cb.head = 0xFFFFFFF8UL;
    cb.tail = 0xFFFFFFF8UL;
    for (unsigned i = 0U; i < 20U; i++) {
        CHECK(io_cb_put_from_isr(&cb, (uint8_t)(0x40U + i), &w));
    }
    CHECK_EQ_INT(cb.head, 12);                               /* wrapped */
    CHECK_EQ_INT(io_cb_count(&cb), 20);
    CHECK_EQ_INT(io_cb_read(&cb, out, sizeof out, 0U, &st), 20);
    CHECK_EQ_INT(out[0], 0x40);
    CHECK_EQ_INT(out[19], 0x53);
    CHECK_EQ_INT(io_cb_count(&cb), 0);

    /* clear drops what is waiting */
    (void)io_cb_put_from_isr(&cb, 1U, &w);
    (void)io_cb_put_from_isr(&cb, 2U, &w);
    io_cb_clear(&cb);
    CHECK_EQ_INT(io_cb_count(&cb), 0);
    CHECK_EQ_INT(io_cb_read(&cb, out, 4U, 5U, &st), 0);
    CHECK_EQ_INT(st, IO_ERR_TIMEOUT);
    /* re-init resets indices and the overrun count */
    CHECK_EQ_INT(io_cb_init(&cb, cb_mem, 32U), IO_OK);
    CHECK_EQ_INT(cb.overruns, 0);
    CHECK_EQ_INT(cb.size, 32);
}

/* ------------------------------------------------------------------ character queue */
static io_cq_t cq;
static uint8_t cq_mem[8];
static unsigned kicks;
static uint8_t drained[64];
static unsigned n_drained;

static void kick(void *ctx)
{
    (void)ctx;
    kicks++;
}

static void isr_drains_all(void)
{
    BaseType_t w;
    uint8_t b;
    while (io_cq_get_from_isr(&cq, &b, &w)) {
        drained[n_drained++] = b;
    }
}

static void isr_sends_three(void)
{
    BaseType_t w;
    (void)io_cq_put_from_isr(&cq, 'x', &w);
    (void)io_cq_put_from_isr(&cq, 'y', &w);
    (void)io_cq_put_from_isr(&cq, 'z', &w);
}

static void test_char_queue(void)
{
    reset_stubs();
    io_status_t st;
    uint8_t in[16];
    uint8_t msg[20];
    for (unsigned i = 0U; i < sizeof msg; i++) {
        msg[i] = (uint8_t)('a' + i);
    }

    memset(&cq, 0, sizeof cq);
    CHECK_EQ_INT(io_cq_init(&cq, cq_mem, 0U), IO_ERR_PARAM);
    CHECK_EQ_INT(io_cq_init(&cq, NULL, 8U), IO_ERR_PARAM);
    CHECK_EQ_INT(io_cq_init(&cq, cq_mem, 8U), IO_OK);
    CHECK_EQ_INT(io_cq_init(&cq, cq_mem, 4U), IO_ERR_STATE);   /* length fixed once created */

    /* fits: queued, one kick at the end, no wait */
    kicks = 0U;
    CHECK_EQ_INT(io_cq_write(&cq, msg, 5U, 100U, kick, NULL, &st), 5);
    CHECK_EQ_INT(st, IO_OK);
    CHECK_EQ_INT(kicks, 1);
    CHECK_EQ_INT(io_cq_count(&cq), 5);

    /* does not fit: kick before blocking, the "ISR" drains, the write completes */
    n_drained = 0U;
    kicks = 0U;
    stub_on_block = isr_drains_all;
    CHECK_EQ_INT(io_cq_write(&cq, &msg[5], 10U, 100U, kick, NULL, &st), 10);
    CHECK_EQ_INT(st, IO_OK);
    CHECK(kicks >= 2U);
    isr_drains_all();
    stub_on_block = NULL;
    CHECK_EQ_INT(n_drained, 15);
    CHECK(memcmp(drained, msg, 15U) == 0);                 /* order kept */

    /* nobody drains: queued up to full, then timeout with a short count */
    kicks = 0U;
    TickType_t t0 = stub_tick;
    CHECK_EQ_INT(io_cq_write(&cq, msg, 12U, 40U, kick, NULL, &st), 8);
    CHECK_EQ_INT(st, IO_ERR_TIMEOUT);
    CHECK_EQ_INT(stub_tick - t0, 40);
    CHECK(kicks >= 2U);                                     /* before the wait and at the end */
    CHECK_EQ_INT(io_cq_wait_empty(&cq, 10U), IO_ERR_TIMEOUT);
    stub_on_block = isr_drains_all;
    n_drained = 0U;
    CHECK_EQ_INT(io_cq_wait_empty(&cq, 10U), IO_OK);       /* drained during the delay */
    stub_on_block = NULL;
    CHECK_EQ_INT(n_drained, 8);

    /* RX: the ISR fills, the task reads exactly len or times out */
    BaseType_t w;
    CHECK(io_cq_put_from_isr(&cq, '1', &w));
    CHECK(io_cq_put_from_isr(&cq, '2', &w));
    CHECK_EQ_INT(io_cq_read(&cq, in, 2U, 10U, &st), 2);
    CHECK_EQ_INT(st, IO_OK);
    CHECK_EQ_INT(in[1], '2');
    t0 = stub_tick;
    CHECK(io_cq_put_from_isr(&cq, '3', &w));
    CHECK_EQ_INT(io_cq_read(&cq, in, 4U, 25U, &st), 1);
    CHECK_EQ_INT(st, IO_ERR_TIMEOUT);
    CHECK_EQ_INT(in[0], '3');
    CHECK_EQ_INT(stub_tick - t0, 25);
    stub_on_block = isr_sends_three;
    CHECK_EQ_INT(io_cq_read(&cq, in, 3U, 100U, &st), 3);
    stub_on_block = NULL;
    CHECK_EQ_INT(in[2], 'z');

    /* ISR side when full / empty, clear and re-init */
    for (unsigned i = 0U; i < 8U; i++) {
        CHECK(io_cq_put_from_isr(&cq, (uint8_t)i, &w));
    }
    CHECK(!io_cq_put_from_isr(&cq, 9U, &w));
    io_cq_clear(&cq);
    CHECK_EQ_INT(io_cq_count(&cq), 0);
    uint8_t b;
    CHECK(!io_cq_get_from_isr(&cq, &b, &w));
    CHECK(io_cq_put_from_isr(&cq, 7U, &w));
    CHECK_EQ_INT(io_cq_init(&cq, cq_mem, 8U), IO_OK);       /* same length: emptied */
    CHECK_EQ_INT(io_cq_count(&cq), 0);
}

/* the UART2 board default (character-queue TX) through io_ioctl */
static void test_char_queue_ioctl(void)
{
    reset_stubs();
    io_handle_t u2 = io_open("/UART2/", 0U);
    CHECK_EQ_INT(io_ioctl(u2, IO_IOCTL_WAIT_TX_EMPTY, IO_VAL(5U)), IO_OK);   /* empty already */
    BaseType_t w;
    CHECK(io_cq_put_from_isr(u2->txq, 'q', &w));
    CHECK_EQ_INT(io_ioctl(u2, IO_IOCTL_WAIT_TX_EMPTY, IO_VAL(5U)), IO_ERR_TIMEOUT);
    CHECK_EQ_INT(io_ioctl(u2, IO_IOCTL_USE_CHAR_QUEUE_TX, IO_VAL(16U)), IO_OK);   /* reselect empties */
    CHECK_EQ_INT(io_ioctl(u2, IO_IOCTL_WAIT_TX_EMPTY, IO_VAL(5U)), IO_OK);
    CHECK_EQ_INT(io_ioctl(u2, IO_IOCTL_USE_CHAR_QUEUE_RX, IO_VAL(4U)), IO_ERR_NO_MEMORY);
    CHECK_EQ_INT(u2->rx_mode, IO_MODE_POLLED);
}

/* ------------------------------------------------------------------ polled wait */
static unsigned polls;
static unsigned ready_after;

static bool ready_fn(void *ctx)
{
    (void)ctx;
    return ++polls > ready_after;
}

static bool never_ready(void *ctx)
{
    (void)ctx;
    polls++;
    return false;
}

static void test_polled(void)
{
    host_ms = 1000U;
    host_ms_step = 1U;
    polls = 0U;
    ready_after = 0U;
    CHECK_EQ_INT(io_polled_wait(ready_fn, NULL, 10U), IO_OK);
    CHECK_EQ_INT(polls, 1);
    polls = 0U;
    ready_after = 5U;
    CHECK_EQ_INT(io_polled_wait(ready_fn, NULL, 10U), IO_OK);
    CHECK_EQ_INT(polls, 6);
    polls = 0U;
    uint32_t t0 = host_ms;
    CHECK_EQ_INT(io_polled_wait(never_ready, NULL, 10U), IO_ERR_TIMEOUT);
    CHECK(host_ms - t0 >= 10U);
    CHECK(polls >= 10U);
    CHECK_EQ_INT(io_polled_wait(NULL, NULL, 10U), IO_ERR_PARAM);

    /* clock wrap during the wait */
    host_ms = 0xFFFFFFFCUL;
    polls = 0U;
    CHECK_EQ_INT(io_polled_wait(never_ready, NULL, 6U), IO_ERR_TIMEOUT);
    CHECK(polls >= 6U && polls < 20U);
}

int main(void)
{
    RUN(test_paths);
    RUN(test_open);
    RUN(test_read_write);
    RUN(test_ioctl_check);
    RUN(test_modes);
    RUN(test_zero_copy_bus);
    RUN(test_zero_copy_isr);
    RUN(test_circ_buf);
    RUN(test_char_queue);
    RUN(test_char_queue_ioctl);
    RUN(test_polled);
    TEST_SUMMARY("test_io");
}
