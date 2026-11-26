/*
 * ENC28J60 driver.
 *
 * Register layer - rules from the data sheet and errata enforced here:
 *  - Banked registers: select the bank in ECON1.BSEL before access. 0x1B..0x1F
 *    (EIE, EIR, ESTAT, ECON2, ECON1) are in every bank and never need a switch.
 *    The current bank is cached; a wrong bank silently hits another register.
 *  - MAC and MII registers return a dummy byte before the data on RCR.
 *  - BFS/BFC only work on ETH registers (never on MAC/MII).
 *  - After a system reset command (SRC) CLKRDY is not cleared, so polling it
 *    proves nothing: wait at least 1 ms, then check CLKRDY.
 *  - PHY registers are reached through MIREGADR/MICMD/MISTAT; poll MISTAT.BUSY.
 *
 * Packet layer - buffer errata:
 *  - RX FIFO at 0000h (ERXST = 0), TX above it. Writing ERXST/ERXND can load
 *    0000h into the internal write pointer, which is harmless only with RX at 0.
 *  - ERXRDPT must be odd: if the next packet pointer equals ERXST use ERXND,
 *    else next - 1. ERXND is odd and the FIFO does not wrap past 1FFFh.
 *  - ERXRDPT is written L then H; only the H write latches.
 *  - PKTIF is unreliable: EPKTCNT is polled. Every frame is freed with
 *    ECON2.PKTDEC, otherwise EPKTCNT saturates at 255 and reception stops.
 *  - TX can stall: ECON1.TXRST is pulsed before every transmission, TXIF or TXERIF
 *    both mean "done", and late collisions are read from TSV bit 29 (not ESTAT).
 *  - ERXST/ERXND are only written while RXEN is clear.
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

#define ENC_RESET_WAIT_MS     2U        /* errata: >= 1 ms after SRC */
#define ENC_CLKRDY_TIMEOUT_MS 10U
#define ENC_MII_TIMEOUT_MS    5U
#define ENC_TX_TIMEOUT_MS     20U       /* 1518 bytes = 1.2 ms on the wire + back-off */
#define ENC_TX_RETRIES        3U        /* late-collision retries */

/* 8 KB buffer: RX 0000h-19FFh (6.5 KB), TX 1A00h-1FFFh (1.5 KB) */
#define RX_START   0x0000U
#define RX_END     0x19FFU              /* odd, as the ERXRDPT rule needs */
#define TX_START   0x1A00U              /* even; control byte + 1518 + 7-byte TSV fits */

static const spi_device_t enc_dev = {
    .name    = "enc28j60",
    .cs_port = BOARD_ENC_CS_PORT,
    .cs_pin  = BOARD_ENC_CS_PIN,
};

static uint8_t  cur_bank = 0xFFU;       /* unknown until the first select */
static uint8_t  revision;
static bool     spi_failed;
static uint16_t next_packet;            /* where the next RX header starts */
static uint16_t rx_remaining;           /* frame bytes not yet read in this rx_begin/rx_end */
static uint16_t tx_len;                 /* frame bytes written since tx_begin */
static enc_stats_t stats;
static TaskHandle_t volatile irq_task;

/* ---------------------------------------------------------------- helpers */

static void enc_delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms) + 1U);
    } else {
        board_delay_busy_ms(ms + 1U);      /* HAL tick may be masked before the scheduler */
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

/* locked: read buffer memory from ERDPT on (auto-increment wraps ERXND -> ERXST) */
static void buf_read(uint8_t *dst, uint16_t n)
{
    if (n == 0U) {
        return;
    }
    uint8_t op = ENC_OP_RBM;
    spi_dev_select(&enc_dev);
    if (spi_bus_transfer(&op, NULL, 1U) != SPI_OK || spi_bus_transfer(NULL, dst, n) != SPI_OK) {
        spi_failed = true;
    }
    spi_dev_deselect(&enc_dev);
}

/* locked: write buffer memory from EWRPT on */
static void buf_write(const uint8_t *src, uint16_t n)
{
    if (n == 0U) {
        return;
    }
    uint8_t op = ENC_OP_WBM;
    spi_dev_select(&enc_dev);
    if (spi_bus_transfer(&op, NULL, 1U) != SPI_OK || spi_bus_transfer(src, NULL, n) != SPI_OK) {
        spi_failed = true;
    }
    spi_dev_deselect(&enc_dev);
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

/* ---------------------------------------------------------------- set-up */

/* locked. RXEN must be clear. */
static void rx_buffer_init(void)
{
    next_packet = RX_START;
    enc_reg_write16(ERXSTL, RX_START);
    enc_reg_write16(ERXNDL, RX_END);
    enc_reg_write16(ERXRDPTL, RX_END);      /* odd: NPP == ERXST case -> ERXND */
    enc_reg_write16(ERDPTL, RX_START);
    enc_reg_write16(ETXSTL, TX_START);
}

/* locked. Half duplex: the safe default when the link partner auto-negotiates
   (the ENC28J60 PHY does not auto-negotiate). */
static enc_status_t mac_phy_setup(const uint8_t mac[6])
{
    enc_bit_clear(ECON1, ECON1_RXEN);            /* receiver off while configuring */
    enc_bit_set(ECON2, ECON2_AUTOINC);           /* buffer pointers auto-increment (reset default) */

    rx_buffer_init();
    /* unicast to our MAC + broadcast, drop frames with a bad CRC */
    enc_reg_write(ERXFCON, ERXFCON_UCEN | ERXFCON_CRCEN | ERXFCON_BCEN);

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

/* locked: recover from a corrupt RX header (bad next pointer). */
static void rx_reset(void)
{
    enc_bit_clear(ECON1, ECON1_RXEN);
    enc_bit_set(ECON1, ECON1_RXRST);
    enc_bit_clear(ECON1, ECON1_RXRST);
    rx_buffer_init();
    enc_bit_clear(EIR, EIR_RXERIF);
    enc_bit_set(ECON1, ECON1_RXEN);
    stats.rx_resets++;
}

/* locked: free the current frame (odd ERXRDPT rule, then PKTDEC). */
static void rx_free(void)
{
    uint16_t rdpt = (next_packet == RX_START) ? RX_END : (uint16_t)(next_packet - 1U);
    enc_reg_write16(ERXRDPTL, rdpt);
    enc_bit_set(ECON2, ECON2_PKTDEC);
}

/* ---------------------------------------------------------------- public: control */

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

enc_status_t enc28j60_start(TaskHandle_t notify_task)
{
    if (revision == 0U) {
        return ENC_ERR_NO_CHIP;
    }
    irq_task = notify_task;
    if (spi_bus_lock(ENC_LOCK_TIMEOUT_MS) != SPI_OK) {
        return ENC_ERR_SPI;
    }
    (void)enc_phy_write(PHIE, PHIE_PGEIE | PHIE_PLNKIE);              /* link change -> LINKIF */
    enc_bit_clear(EIR, EIR_PKTIF | EIR_LINKIF | EIR_TXIF | EIR_TXERIF | EIR_RXERIF);
    enc_reg_write(EIE, EIE_INTIE | EIE_PKTIE | EIE_LINKIE | EIE_RXERIE);
    enc_bit_set(ECON1, ECON1_RXEN);
    spi_bus_unlock();

    board_enc_int_enable();
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

uint32_t enc28j60_service(void)
{
    uint32_t events = 0U;

    if (revision == 0U || spi_bus_lock(ENC_LOCK_TIMEOUT_MS) != SPI_OK) {
        return 0U;
    }
    /* INTIE off while we look, back on at the end: if anything is still pending the
       INT pin goes high and low again, which gives the EXTI a fresh falling edge. */
    enc_bit_clear(EIE, EIE_INTIE);
    uint8_t eir = enc_reg_read(EIR);
    if ((eir & EIR_LINKIF) != 0U) {
        (void)enc_phy_read(PHIR);                 /* reading PHIR clears LINKIF */
        events |= ENC_EVT_LINK;
    }
    if ((eir & EIR_RXERIF) != 0U) {               /* FIFO full or EPKTCNT at 255 */
        enc_bit_clear(EIR, EIR_RXERIF);
        stats.rx_overflows++;
        events |= ENC_EVT_RX_ERR;
    }
    if (enc_reg_read(EPKTCNT) != 0U) {            /* PKTIF is unreliable: use the count */
        events |= ENC_EVT_RX;
    }
    enc_bit_set(EIE, EIE_INTIE);
    spi_bus_unlock();
    return events;
}

void enc28j60_get_stats(enc_stats_t *out)
{
    *out = stats;
}

void enc28j60_int_isr(void)
{
    BaseType_t woken = pdFALSE;
    TaskHandle_t t = irq_task;

    stats.irqs++;
    if (t != NULL) {
        vTaskNotifyGiveFromISR(t, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

/* ---------------------------------------------------------------- public: receive */

enc_status_t enc28j60_rx_begin(uint16_t *len)
{
    if (revision == 0U) {
        return ENC_ERR_NO_CHIP;
    }
    if (spi_bus_lock(ENC_LOCK_TIMEOUT_MS) != SPI_OK) {
        return ENC_ERR_SPI;
    }
    if (enc_reg_read(EPKTCNT) == 0U) {
        spi_bus_unlock();
        return ENC_ERR_EMPTY;
    }

    uint8_t hdr[6];
    enc_reg_write16(ERDPTL, next_packet);
    buf_read(hdr, sizeof hdr);
    uint16_t npp   = (uint16_t)(hdr[0] | ((uint16_t)hdr[1] << 8));
    uint32_t rsv   = (uint32_t)hdr[2] | ((uint32_t)hdr[3] << 8) | ((uint32_t)hdr[4] << 16) | ((uint32_t)hdr[5] << 24);
    uint16_t count = (uint16_t)(rsv & 0xFFFFU);           /* includes the 4-byte CRC */

    if (npp > RX_END || (npp & 1U) != 0U) {
        rx_reset();                                       /* header is garbage: start over */
        spi_bus_unlock();
        return ENC_ERR_RX;
    }
    next_packet = npp;

    if ((rsv & RSV_RXOK) == 0U || count < 18U || count > ENC_MAX_FRAME + 4U) {
        stats.rx_dropped++;
        rx_free();
        spi_bus_unlock();
        return ENC_ERR_RX;
    }
    rx_remaining = (uint16_t)(count - 4U);
    *len = rx_remaining;
    return ENC_OK;                                        /* bus stays locked until rx_end */
}

void enc28j60_rx_read(uint8_t *dst, uint16_t n)
{
    if (n > rx_remaining) {
        n = rx_remaining;
    }
    buf_read(dst, n);
    rx_remaining = (uint16_t)(rx_remaining - n);
}

void enc28j60_rx_end(void)
{
    rx_free();
    rx_remaining = 0U;
    stats.rx_frames++;
    spi_bus_unlock();
}

uint16_t enc28j60_receive(uint8_t *buf, uint16_t size)
{
    uint16_t len = 0U;
    if (enc28j60_rx_begin(&len) != ENC_OK) {
        return 0U;
    }
    if (len > size) {
        stats.rx_dropped++;
        len = 0U;
    } else {
        enc28j60_rx_read(buf, len);
    }
    enc28j60_rx_end();
    return len;
}

/* ---------------------------------------------------------------- public: transmit */

enc_status_t enc28j60_tx_begin(void)
{
    if (revision == 0U) {
        return ENC_ERR_NO_CHIP;
    }
    if (spi_bus_lock(ENC_LOCK_TIMEOUT_MS) != SPI_OK) {
        return ENC_ERR_SPI;
    }
    spi_failed = false;
    tx_len = 0U;
    enc_reg_write16(EWRPTL, TX_START);
    uint8_t control = 0x00U;                     /* use the MACON3 settings for this frame */
    buf_write(&control, 1U);
    return ENC_OK;                               /* bus stays locked until tx_end */
}

void enc28j60_tx_write(const uint8_t *src, uint16_t n)
{
    if ((uint32_t)tx_len + n > ENC_MAX_FRAME - 4U) {     /* the MAC appends the CRC */
        spi_failed = true;
        return;
    }
    buf_write(src, n);
    tx_len = (uint16_t)(tx_len + n);
}

/* locked: one transmission attempt; returns the 7-byte TSV in tsv[] */
static enc_status_t tx_once(uint16_t tx_end, uint8_t tsv[7])
{
    /* errata: reset the TX logic before every transmission */
    enc_bit_set(ECON1, ECON1_TXRST);
    enc_bit_clear(ECON1, ECON1_TXRST);
    enc_bit_clear(EIR, EIR_TXIF | EIR_TXERIF);

    enc_reg_write16(ETXSTL, TX_START);
    enc_reg_write16(ETXNDL, tx_end);
    enc_bit_set(ECON1, ECON1_TXRTS);

    uint32_t t0 = HAL_GetTick();
    enc_status_t st = ENC_OK;
    while ((enc_reg_read(EIR) & (EIR_TXIF | EIR_TXERIF)) == 0U) {   /* either flag = done */
        if (HAL_GetTick() - t0 > ENC_TX_TIMEOUT_MS) {
            st = ENC_ERR_TIMEOUT;
            break;
        }
    }
    enc_bit_clear(ECON1, ECON1_TXRTS);

    enc_reg_write16(ERDPTL, (uint16_t)(tx_end + 1U));                /* TSV follows ETXND */
    buf_read(tsv, 7U);
    return st;
}

enc_status_t enc28j60_tx_end(void)
{
    enc_status_t st = ENC_OK;

    if (spi_failed || tx_len < 14U) {
        st = ENC_ERR_PARAM;
    } else {
        uint16_t tx_end = (uint16_t)(TX_START + tx_len);    /* control byte + tx_len bytes */
        for (unsigned attempt = 0U; attempt <= ENC_TX_RETRIES; attempt++) {
            uint8_t tsv[7] = { 0 };
            st = tx_once(tx_end, tsv);
            uint8_t estat = enc_reg_read(ESTAT);
            uint64_t v = 0U;
            for (int i = 6; i >= 0; i--) {
                v = (v << 8) | tsv[i];
            }
            if (st == ENC_OK && (estat & ESTAT_TXABRT) == 0U && (v & TSV_DONE_OK) != 0U) {
                break;                                      /* sent */
            }
            if ((v & TSV_LATE_COLLISION) != 0U && attempt < ENC_TX_RETRIES) {
                stats.tx_retries++;                         /* errata: retry on late collision */
                continue;
            }
            if ((v & TSV_EXCESSIVE_COLLISION) != 0U) {
                stats.tx_collisions++;
            }
            if (st == ENC_OK) {
                st = ENC_ERR_TX;
            }
            break;
        }
    }
    if (st == ENC_OK) {
        stats.tx_frames++;
    } else {
        stats.tx_errors++;
    }
    spi_bus_unlock();
    return st;
}

enc_status_t enc28j60_send(const uint8_t *frame, uint16_t len)
{
    enc_status_t st = enc28j60_tx_begin();
    if (st != ENC_OK) {
        return st;
    }
    enc28j60_tx_write(frame, len);
    return enc28j60_tx_end();
}
