/*
 * ENC28J60 driver - register layer.
 *
 * Rules from the data sheet and errata that this file enforces:
 *  - Banked registers: select the bank in ECON1.BSEL before access. 0x1B..0x1F
 *    (EIE, EIR, ESTAT, ECON2, ECON1) are in every bank and never need a switch.
 *    The current bank is cached; a wrong bank silently hits another register.
 *  - MAC and MII registers return a dummy byte before the data on RCR.
 *  - BFS/BFC only work on ETH registers (never on MAC/MII).
 *  - After a system reset command (SRC) CLKRDY is not cleared, so polling it
 *    proves nothing: wait at least 1 ms, then check CLKRDY.
 *  - PHY registers are reached through MIREGADR/MICMD/MISTAT; poll MISTAT.BUSY.
 *
 * All functions marked "locked" expect the caller to hold the SPI bus lock.
 */
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#define LOG_TAG LOG_MOD_ENC
#include "log.h"

#include "board.h"
#include "enc28j60.h"
#include "enc28j60_regs.h"
#include "spi_bus.h"

#define ENC_LOCK_TIMEOUT_MS   50U
#define ENC_RESET_WAIT_MS     2U        /* errata: >= 1 ms after SRC */
#define ENC_CLKRDY_TIMEOUT_MS 10U
#define ENC_MII_TIMEOUT_MS    5U
#define ENC_MAX_FRAME         1518U

static const spi_device_t enc_dev = {
    .name    = "enc28j60",
    .cs_port = BOARD_ENC_CS_PORT,
    .cs_pin  = BOARD_ENC_CS_PIN,
};

static uint8_t cur_bank = 0xFFU;        /* unknown until the first select */
static uint8_t revision;
static bool    spi_failed;

/* ---------------------------------------------------------------- helpers */

static void enc_delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms) + 1U);
    } else {
        HAL_Delay(ms + 1U);
    }
}

static void cycle(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_dev_select(&enc_dev);
    if (spi_bus_transfer(tx, rx, len) != SPI_OK) {
        spi_failed = true;
    }
    spi_dev_deselect(&enc_dev);
}

/* locked */
static void op_write(uint8_t op, uint8_t addr, uint8_t data)
{
    uint8_t tx[2] = { (uint8_t)(op | (addr & ENC_ADDR_MASK)), data };
    cycle(tx, NULL, sizeof tx);
}

/* locked */
static uint8_t op_read(uint8_t addr, bool mac_mii)
{
    uint8_t tx[3] = { (uint8_t)(ENC_OP_RCR | (addr & ENC_ADDR_MASK)), 0xFFU, 0xFFU };
    uint8_t rx[3] = { 0 };
    size_t n = mac_mii ? 3U : 2U;      /* MAC/MII: opcode, dummy, data */
    cycle(tx, rx, n);
    return rx[n - 1U];
}

/* locked */
static void select_bank(uint8_t reg)
{
    if ((reg & ENC_ADDR_MASK) >= ENC_COMMON_FIRST) {
        return;
    }
    uint8_t bank = (uint8_t)((reg & ENC_BANK_MASK) >> ENC_BANK_SHIFT);
    if (bank != cur_bank) {
        op_write(ENC_OP_BFC, ECON1, ECON1_BSEL1 | ECON1_BSEL0);
        if (bank != 0U) {
            op_write(ENC_OP_BFS, ECON1, bank);
        }
        cur_bank = bank;
    }
}

/* locked */
static uint8_t enc_reg_read(uint8_t reg)
{
    select_bank(reg);
    return op_read(reg, (reg & ENC_MACMII) != 0U);
}

/* locked */
static void enc_reg_write(uint8_t reg, uint8_t val)
{
    select_bank(reg);
    op_write(ENC_OP_WCR, reg, val);
}

/* locked. Writes L then H: for ERXRDPT only the H write latches the new value. */
static void enc_reg_write16(uint8_t reg_l, uint16_t val)
{
    enc_reg_write(reg_l, (uint8_t)(val & 0xFFU));
    enc_reg_write((uint8_t)(reg_l + 1U), (uint8_t)(val >> 8));
}

/* locked. ETH registers only. */
static void enc_bit_set(uint8_t reg, uint8_t mask)
{
    configASSERT((reg & ENC_MACMII) == 0U);
    select_bank(reg);
    op_write(ENC_OP_BFS, reg, mask);
}

/* locked. ETH registers only. */
static void enc_bit_clear(uint8_t reg, uint8_t mask)
{
    configASSERT((reg & ENC_MACMII) == 0U);
    select_bank(reg);
    op_write(ENC_OP_BFC, reg, mask);
}

/* locked */
static bool mii_wait(void)
{
    uint32_t t0 = HAL_GetTick();
    while ((enc_reg_read(MISTAT) & MISTAT_BUSY) != 0U) {
        if (HAL_GetTick() - t0 > ENC_MII_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

/* locked */
static uint16_t enc_phy_read(uint8_t phy_reg)
{
    enc_reg_write(MIREGADR, phy_reg);
    enc_reg_write(MICMD, MICMD_MIIRD);
    (void)mii_wait();                       /* 10.24 us typical */
    enc_reg_write(MICMD, 0U);
    uint16_t lo = enc_reg_read(MIRDL);
    uint16_t hi = enc_reg_read(MIRDH);
    return (uint16_t)((hi << 8) | lo);
}

/* locked */
static bool enc_phy_write(uint8_t phy_reg, uint16_t val)
{
    enc_reg_write(MIREGADR, phy_reg);
    enc_reg_write(MIWRL, (uint8_t)(val & 0xFFU));
    enc_reg_write(MIWRH, (uint8_t)(val >> 8));    /* writing H starts the MII transaction */
    return mii_wait();
}

/* ---------------------------------------------------------------- reset */

/* locked */
static enc_status_t soft_reset(void)
{
    uint8_t cmd = ENC_OP_SRC;
    cycle(&cmd, NULL, 1U);
    cur_bank = 0U;                          /* ECON1 resets to 00h: bank 0 */
    enc_delay_ms(ENC_RESET_WAIT_MS);        /* errata: CLKRDY is not cleared by SRC */

    uint32_t t0 = HAL_GetTick();
    while ((enc_reg_read(ESTAT) & ESTAT_CLKRDY) == 0U) {
        if (HAL_GetTick() - t0 > ENC_CLKRDY_TIMEOUT_MS) {
            return ENC_ERR_TIMEOUT;
        }
    }
    return ENC_OK;
}

static void hard_reset(void)
{
    HAL_GPIO_WritePin(BOARD_ENC_RST_PORT, BOARD_ENC_RST_PIN, GPIO_PIN_RESET);
    enc_delay_ms(1U);                       /* >= 400 ns low */
    HAL_GPIO_WritePin(BOARD_ENC_RST_PORT, BOARD_ENC_RST_PIN, GPIO_PIN_SET);
    enc_delay_ms(1U);                       /* oscillator start-up ~300 us */
}

/* ---------------------------------------------------------------- MAC / PHY set-up */

/* locked. Half duplex: the safe default when the link partner auto-negotiates
   (the ENC28J60 PHY does not auto-negotiate). */
static enc_status_t mac_phy_setup(const uint8_t mac[6])
{
    enc_bit_clear(ECON1, ECON1_RXEN);            /* receiver off while configuring */
    enc_bit_set(ECON2, ECON2_AUTOINC);           /* buffer pointers auto-increment (reset default) */

    enc_reg_write(MACON1, MACON1_MARXEN);
    enc_reg_write(MACON3, MACON3_PADCFG0 | MACON3_TXCRCEN | MACON3_FRMLNEN);
    enc_reg_write(MACON4, MACON4_DEFER);
    enc_reg_write16(MAMXFLL, ENC_MAX_FRAME);
    enc_reg_write(MABBIPG, 0x12U);          /* back-to-back gap, half duplex */
    enc_reg_write(MAIPGL, 0x12U);
    enc_reg_write(MAIPGH, 0x0CU);

    enc_reg_write(MAADR1, mac[0]);
    enc_reg_write(MAADR2, mac[1]);
    enc_reg_write(MAADR3, mac[2]);
    enc_reg_write(MAADR4, mac[3]);
    enc_reg_write(MAADR5, mac[4]);
    enc_reg_write(MAADR6, mac[5]);

    if (!enc_phy_write(PHCON1, 0U)) {                    /* PDPXMD = 0: half duplex */
        return ENC_ERR_TIMEOUT;
    }
    if (!enc_phy_write(PHCON2, PHCON2_HDLDIS)) {         /* no loopback of our own TX */
        return ENC_ERR_TIMEOUT;
    }
    (void)enc_phy_write(PHLCON, 0x3476U);                /* LEDA link, LEDB activity, stretch */
    return ENC_OK;
}

/* ---------------------------------------------------------------- public */

enc_status_t enc28j60_init(const uint8_t mac[6])
{
    if (mac == NULL) {
        return ENC_ERR_PARAM;
    }
    hard_reset();
    if (spi_bus_lock(ENC_LOCK_TIMEOUT_MS) != SPI_OK) {
        return ENC_ERR_SPI;
    }
    spi_failed = false;
    cur_bank = 0xFFU;

    enc_status_t st = soft_reset();
    uint16_t phid1 = 0U;
    if (st == ENC_OK) {
        revision = enc_reg_read(EREVID);
        phid1 = enc_phy_read(PHID1);
        if (revision == 0x00U || revision == 0xFFU || phid1 != PHID1_VALUE) {
            st = ENC_ERR_NO_CHIP;
        }
    }
    if (st == ENC_OK) {
        st = mac_phy_setup(mac);
    }
    if (st == ENC_OK && spi_failed) {
        st = ENC_ERR_SPI;
    }
    spi_bus_unlock();

    if (st == ENC_ERR_NO_CHIP || st == ENC_ERR_TIMEOUT) {
        LOG_W("not present (EREVID=0x%02X PHID1=0x%04X)", revision, phid1);
        revision = 0U;
        return st;
    }
    if (st != ENC_OK) {
        LOG_E("init failed (%d)", (int)st);
        return st;
    }
    LOG_I("rev %s", enc28j60_revision_name(revision));
    if (revision == EREVID_B1 || revision == EREVID_B4) {
        LOG_W("rev %s: keep SCK >= 8 MHz (errata)", enc28j60_revision_name(revision));
    }
    LOG_I("MAC %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ENC_OK;
}

uint8_t enc28j60_revision(void)
{
    return revision;
}

const char *enc28j60_revision_name(uint8_t rev)
{
    switch (rev) {
    case EREVID_B1: return "B1";
    case EREVID_B4: return "B4";
    case EREVID_B5: return "B5";
    case EREVID_B7: return "B7";
    default:        return "?";
    }
}

bool enc28j60_link_up(void)
{
    if (revision == 0U || spi_bus_lock(ENC_LOCK_TIMEOUT_MS) != SPI_OK) {
        return false;
    }
    uint16_t st2 = enc_phy_read(PHSTAT2);
    spi_bus_unlock();
    return (st2 & PHSTAT2_LSTAT) != 0U;
}
