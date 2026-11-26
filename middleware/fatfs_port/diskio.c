/*
 * FatFs disk I/O layer on the W25Q SPI NOR flash.
 *
 * One logical sector (LBA) = one 4 KB erase sector, so a sector write is
 * "erase, then program 16 pages" and never touches a neighbour. Drive 0 only.
 *
 * Until the W25Q data path is implemented (step 15 mentee task) w25q_read()
 * returns W25Q_ERR_NOT_READY; disk_initialize() then reports STA_NOINIT and
 * f_mount() fails cleanly, which the storage task treats as "no media".
 */
#include "ff.h"
#include "diskio.h"

#include "w25q.h"

static DSTATUS stat = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0U) ? stat : (DSTATUS)STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U) {
        return STA_NOINIT;
    }
    if (!w25q_present()) {
        stat = STA_NOINIT | STA_NODISK;
        return stat;
    }
    uint8_t probe;
    stat = (w25q_read(0U, &probe, 1U) == W25Q_OK) ? 0U : STA_NOINIT;
    return stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0U || count == 0U) {
        return RES_PARERR;
    }
    if ((stat & STA_NOINIT) != 0U) {
        return RES_NOTRDY;
    }
    if (sector + count > w25q_sector_count()) {
        return RES_PARERR;
    }
    w25q_status_t st = w25q_read((uint32_t)sector * W25Q_SECTOR_SIZE, buff, (size_t)count * W25Q_SECTOR_SIZE);
    return (st == W25Q_OK) ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0U || count == 0U) {
        return RES_PARERR;
    }
    if ((stat & STA_NOINIT) != 0U) {
        return RES_NOTRDY;
    }
    if (sector + count > w25q_sector_count()) {
        return RES_PARERR;
    }
    for (UINT s = 0U; s < count; s++) {
        uint32_t base = ((uint32_t)sector + s) * W25Q_SECTOR_SIZE;
        if (w25q_sector_erase(base) != W25Q_OK) {
            return RES_ERROR;
        }
        for (uint32_t off = 0U; off < W25Q_SECTOR_SIZE; off += W25Q_PAGE_SIZE) {
            if (w25q_page_program(base + off, &buff[s * W25Q_SECTOR_SIZE + off], W25Q_PAGE_SIZE) != W25Q_OK) {
                return RES_ERROR;
            }
        }
    }
    return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0U) {
        return RES_PARERR;
    }
    if ((stat & STA_NOINIT) != 0U) {
        return RES_NOTRDY;
    }
    switch (cmd) {
    case CTRL_SYNC:                    /* every write is already complete on return */
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (LBA_t)w25q_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)W25Q_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:               /* erase block in units of sectors */
        *(DWORD *)buff = 1U;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
