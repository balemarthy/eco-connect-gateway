/*
 * W25Q SPI NOR flash (Winbond W25Q16/32/64/128 and compatibles) on the shared /SPI2/ bus
 * of the IO layer, own chip select IO_SPI2_CS_W25Q (PB1, app/io/io_board_f411.h).
 *
 * Provided: w25q_init() (release from power-down, JEDEC ID, capacity), status/busy helpers.
 * MENTEE TASK: w25q_read(), w25q_page_program(), w25q_sector_erase() return
 * W25Q_ERR_NOT_READY until you implement them. Contract:
 *   - every call takes the bus mutex (IO_IOCTL_OBTAIN_WRITE_MUTEX on /SPI2/), selects
 *     IO_SPI2_CS_W25Q, and deselects and releases on every path (see cmd() in w25q.c),
 *   - program/erase: WRITE ENABLE (06h) first, then the command, then poll
 *     STATUS-1 (05h) BUSY with a timeout (page 3 ms max, 4 KB sector 400 ms max),
 *   - page program never crosses a 256-byte page boundary (the address wraps inside
 *     the page otherwise), sector erase needs a 4 KB aligned address,
 *   - addresses beyond the capacity are W25Q_ERR_PARAM.
 * A logic analyser trace of READ (03h) and PAGE PROGRAM (02h) is the evidence item.
 */
#ifndef W25Q_H
#define W25Q_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define W25Q_PAGE_SIZE      256U
#define W25Q_SECTOR_SIZE    4096U

#define W25Q_CMD_WRITE_ENABLE   0x06U
#define W25Q_CMD_READ_STATUS1   0x05U
#define W25Q_CMD_READ_DATA      0x03U
#define W25Q_CMD_PAGE_PROGRAM   0x02U
#define W25Q_CMD_SECTOR_ERASE   0x20U
#define W25Q_CMD_JEDEC_ID       0x9FU
#define W25Q_CMD_RELEASE_PD     0xABU
#define W25Q_SR1_BUSY           0x01U
#define W25Q_SR1_WEL            0x02U

#define W25Q_PAGE_TIMEOUT_MS    5U
#define W25Q_SECTOR_TIMEOUT_MS  500U

typedef enum {
    W25Q_OK = 0,
    W25Q_ERR_SPI,
    W25Q_ERR_TIMEOUT,
    W25Q_ERR_NOT_PRESENT,
    W25Q_ERR_PARAM,
    W25Q_ERR_NOT_READY      /* not implemented yet (mentee task) */
} w25q_status_t;

typedef struct {
    uint8_t  manufacturer;   /* EFh = Winbond */
    uint16_t device;         /* 4017h = W25Q64 */
    uint32_t capacity;       /* bytes */
} w25q_info_t;

w25q_status_t      w25q_init(void);
bool               w25q_present(void);
const w25q_info_t *w25q_info(void);
uint32_t           w25q_sector_count(void);
w25q_status_t      w25q_read_status(uint8_t *sr1);
w25q_status_t      w25q_wait_ready(uint32_t timeout_ms);

w25q_status_t w25q_read(uint32_t addr, uint8_t *buf, size_t len);
w25q_status_t w25q_page_program(uint32_t addr, const uint8_t *buf, size_t len);
w25q_status_t w25q_sector_erase(uint32_t addr);

const char *w25q_status_str(w25q_status_t st);

#endif /* W25Q_H */
