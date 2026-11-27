/*
 * W25Q SPI NOR driver: probe and helpers implemented, data path left as the
 * mentee task (see w25q.h for the contract).
 */
#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_W25Q
#include "log.h"

#include "board.h"
#include "io.h"
#include "io_board.h"
#include "w25q.h"

#define W25Q_LOCK_TIMEOUT_MS  50U

static io_handle_t spi;                  /* "/SPI2/", shared with the ENC28J60 (bus mutex) */

static w25q_info_t info;
static bool present;

const char *w25q_status_str(w25q_status_t st)
{
    switch (st) {
    case W25Q_OK:              return "ok";
    case W25Q_ERR_SPI:         return "spi error";
    case W25Q_ERR_TIMEOUT:     return "timeout";
    case W25Q_ERR_NOT_PRESENT: return "not present";
    case W25Q_ERR_PARAM:       return "bad parameter";
    case W25Q_ERR_NOT_READY:   return "not implemented";
    default:                   return "?";
    }
}

/* One command = one chip-select cycle under the bus mutex: write tx_len, then read rx_len. */
static w25q_status_t cmd(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len)
{
    io_status_t st = (spi == NULL) ? IO_ERR_NOT_READY
                                   : io_ioctl(spi, IO_IOCTL_OBTAIN_WRITE_MUTEX, IO_VAL(W25Q_LOCK_TIMEOUT_MS));
    if (st != IO_OK) {
        return (st == IO_ERR_TIMEOUT) ? W25Q_ERR_TIMEOUT : W25Q_ERR_SPI;
    }
    st = io_ioctl(spi, IO_IOCTL_SPI_SELECT, IO_VAL(IO_SPI2_CS_W25Q));
    if (st == IO_OK && tx_len > 0U && io_write(spi, tx, tx_len) != tx_len) {
        (void)io_ioctl(spi, IO_IOCTL_GET_LAST_STATUS, &st);
    }
    if (st == IO_OK && rx_len > 0U && io_read(spi, rx, rx_len) != rx_len) {
        (void)io_ioctl(spi, IO_IOCTL_GET_LAST_STATUS, &st);
    }
    (void)io_ioctl(spi, IO_IOCTL_SPI_DESELECT, NULL);
    (void)io_ioctl(spi, IO_IOCTL_RELEASE_WRITE_MUTEX, NULL);
    if (st == IO_ERR_TIMEOUT) {
        return W25Q_ERR_TIMEOUT;
    }
    return (st == IO_OK) ? W25Q_OK : W25Q_ERR_SPI;
}

w25q_status_t w25q_init(void)
{
    uint8_t c = W25Q_CMD_RELEASE_PD;
    uint8_t id[3] = { 0 };

    present = false;
    spi = io_open("/SPI2/", 0U);           /* same handle main() opened */
    (void)cmd(&c, 1U, NULL, 0U);           /* wake up if it was left in power-down */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {   /* tRES1 = 3 us */
        vTaskDelay(1);
    } else {
        board_delay_busy_ms(1U);           /* HAL tick may be masked before the scheduler */
    }

    c = W25Q_CMD_JEDEC_ID;
    w25q_status_t st = cmd(&c, 1U, id, sizeof id);
    if (st != W25Q_OK) {
        LOG_W("not present (%s)", w25q_status_str(st));
        return st;
    }
    info.manufacturer = id[0];
    info.device = (uint16_t)((id[1] << 8) | id[2]);
    /* byte 3 is log2(capacity) for the W25Q family: 15h = 2 MB ... 18h = 16 MB */
    if (id[0] == 0x00U || id[0] == 0xFFU || id[2] < 0x11U || id[2] > 0x19U) {
        LOG_W("not present (JEDEC %02X%02X%02X)", id[0], id[1], id[2]);
        return W25Q_ERR_NOT_PRESENT;
    }
    info.capacity = 1UL << id[2];
    present = true;
    LOG_I("JEDEC %02X%04X, %lu KB", info.manufacturer, info.device, (unsigned long)(info.capacity / 1024U));
    return W25Q_OK;
}

bool w25q_present(void)
{
    return present;
}

const w25q_info_t *w25q_info(void)
{
    return &info;
}

uint32_t w25q_sector_count(void)
{
    return present ? info.capacity / W25Q_SECTOR_SIZE : 0U;
}

w25q_status_t w25q_read_status(uint8_t *sr1)
{
    uint8_t c = W25Q_CMD_READ_STATUS1;
    return cmd(&c, 1U, sr1, 1U);
}

w25q_status_t w25q_wait_ready(uint32_t timeout_ms)
{
    TickType_t t0 = xTaskGetTickCount();
    for (;;) {
        uint8_t sr = 0xFFU;
        w25q_status_t st = w25q_read_status(&sr);
        if (st != W25Q_OK) {
            return st;
        }
        if ((sr & W25Q_SR1_BUSY) == 0U) {
            return W25Q_OK;
        }
        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(timeout_ms)) {
            return W25Q_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
}

/* ---------------------------------------------------------------- MENTEE TASK */

w25q_status_t w25q_read(uint32_t addr, uint8_t *buf, size_t len)
{
    /* 03h + 24-bit address (MSB first), then clock out len bytes; any length,
       the address auto-increments across pages and sectors. */
    (void)addr;
    (void)buf;
    (void)len;
    return present ? W25Q_ERR_NOT_READY : W25Q_ERR_NOT_PRESENT;
}

w25q_status_t w25q_page_program(uint32_t addr, const uint8_t *buf, size_t len)
{
    /* 06h; 02h + address + 1..256 bytes inside one page; wait_ready(PAGE). */
    (void)addr;
    (void)buf;
    (void)len;
    return present ? W25Q_ERR_NOT_READY : W25Q_ERR_NOT_PRESENT;
}

w25q_status_t w25q_sector_erase(uint32_t addr)
{
    /* 06h; 20h + 4 KB aligned address; wait_ready(SECTOR). Erased bytes read FFh. */
    (void)addr;
    return present ? W25Q_ERR_NOT_READY : W25Q_ERR_NOT_PRESENT;
}
