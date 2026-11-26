/*
 * ENC28J60 register map (Microchip DS39662E).
 *
 * Register encoding used by the driver (one byte):
 *   bits 4..0  address inside the bank (0x00..0x1F)
 *   bits 6..5  bank (0..3); ignored for the common registers 0x1B..0x1F
 *   bit  7     MAC/MII register: a read returns a dummy byte first
 */
#ifndef ENC28J60_REGS_H
#define ENC28J60_REGS_H

#define ENC_ADDR_MASK   0x1FU
#define ENC_BANK_SHIFT  5U
#define ENC_BANK_MASK   0x60U
#define ENC_MACMII      0x80U

#define ENC_REG(bank, addr)       ((uint8_t)(((bank) << ENC_BANK_SHIFT) | (addr)))
#define ENC_MREG(bank, addr)      ((uint8_t)(ENC_REG(bank, addr) | ENC_MACMII))

/* ---- SPI opcodes (3-bit opcode + 5-bit argument) ---------------------------- */
#define ENC_OP_RCR      0x00U   /* read control register   */
#define ENC_OP_RBM      0x3AU   /* read buffer memory      */
#define ENC_OP_WCR      0x40U   /* write control register  */
#define ENC_OP_WBM      0x7AU   /* write buffer memory     */
#define ENC_OP_BFS      0x80U   /* bit field set   (ETH registers only) */
#define ENC_OP_BFC      0xA0U   /* bit field clear (ETH registers only) */
#define ENC_OP_SRC      0xFFU   /* system reset command    */

/* ---- common registers, present in every bank (0x1A is reserved) --------------- */
#define EIE        0x1BU
#define EIR        0x1CU
#define ESTAT      0x1DU
#define ECON2      0x1EU
#define ECON1      0x1FU
#define ENC_COMMON_FIRST  0x1BU

/* ---- bank 0: buffer pointers ---------------------------------------------- */
#define ERDPTL     ENC_REG(0, 0x00)
#define ERDPTH     ENC_REG(0, 0x01)
#define EWRPTL     ENC_REG(0, 0x02)
#define EWRPTH     ENC_REG(0, 0x03)
#define ETXSTL     ENC_REG(0, 0x04)
#define ETXSTH     ENC_REG(0, 0x05)
#define ETXNDL     ENC_REG(0, 0x06)
#define ETXNDH     ENC_REG(0, 0x07)
#define ERXSTL     ENC_REG(0, 0x08)
#define ERXSTH     ENC_REG(0, 0x09)
#define ERXNDL     ENC_REG(0, 0x0A)
#define ERXNDH     ENC_REG(0, 0x0B)
#define ERXRDPTL   ENC_REG(0, 0x0C)
#define ERXRDPTH   ENC_REG(0, 0x0D)
#define ERXWRPTL   ENC_REG(0, 0x0E)
#define ERXWRPTH   ENC_REG(0, 0x0F)

/* ---- bank 1: filters, packet count ---------------------------------------- */
#define EPMM0      ENC_REG(1, 0x08)
#define EPMCSL     ENC_REG(1, 0x10)
#define EPMCSH     ENC_REG(1, 0x11)
#define ERXFCON    ENC_REG(1, 0x18)
#define EPKTCNT    ENC_REG(1, 0x19)

/* ---- bank 2: MAC and MII (dummy byte on read) ------------------------------ */
#define MACON1     ENC_MREG(2, 0x00)
#define MACON3     ENC_MREG(2, 0x02)
#define MACON4     ENC_MREG(2, 0x03)
#define MABBIPG    ENC_MREG(2, 0x04)
#define MAIPGL     ENC_MREG(2, 0x06)
#define MAIPGH     ENC_MREG(2, 0x07)
#define MACLCON1   ENC_MREG(2, 0x08)
#define MACLCON2   ENC_MREG(2, 0x09)
#define MAMXFLL    ENC_MREG(2, 0x0A)
#define MAMXFLH    ENC_MREG(2, 0x0B)
#define MICMD      ENC_MREG(2, 0x12)
#define MIREGADR   ENC_MREG(2, 0x14)
#define MIWRL      ENC_MREG(2, 0x16)
#define MIWRH      ENC_MREG(2, 0x17)
#define MIRDL      ENC_MREG(2, 0x18)
#define MIRDH      ENC_MREG(2, 0x19)

/* ---- bank 3: MAC address, MII status, revision ------------------------------ */
#define MAADR5     ENC_MREG(3, 0x00)
#define MAADR6     ENC_MREG(3, 0x01)
#define MAADR3     ENC_MREG(3, 0x02)
#define MAADR4     ENC_MREG(3, 0x03)
#define MAADR1     ENC_MREG(3, 0x04)
#define MAADR2     ENC_MREG(3, 0x05)
#define MISTAT     ENC_MREG(3, 0x0A)
#define EREVID     ENC_REG(3, 0x12)     /* ETH register: no dummy byte */
#define ECOCON     ENC_REG(3, 0x15)

/* ---- bits ------------------------------------------------------------------ */
/* EIE */
#define EIE_INTIE    0x80U
#define EIE_PKTIE    0x40U
#define EIE_LINKIE   0x10U
#define EIE_TXIE     0x08U
#define EIE_TXERIE   0x02U
#define EIE_RXERIE   0x01U
/* EIR */
#define EIR_PKTIF    0x40U
#define EIR_LINKIF   0x10U
#define EIR_TXIF     0x08U
#define EIR_TXERIF   0x02U
#define EIR_RXERIF   0x01U
/* ESTAT */
#define ESTAT_INT     0x80U
#define ESTAT_BUFER   0x40U
#define ESTAT_LATECOL 0x10U
#define ESTAT_RXBUSY  0x04U
#define ESTAT_TXABRT  0x02U
#define ESTAT_CLKRDY  0x01U
/* ECON2 */
#define ECON2_AUTOINC 0x80U
#define ECON2_PKTDEC  0x40U
#define ECON2_PWRSV   0x20U
#define ECON2_VRPS    0x08U
/* ECON1 */
#define ECON1_TXRST   0x80U
#define ECON1_RXRST   0x40U
#define ECON1_DMAST   0x20U
#define ECON1_CSUMEN  0x10U
#define ECON1_TXRTS   0x08U
#define ECON1_RXEN    0x04U
#define ECON1_BSEL1   0x02U
#define ECON1_BSEL0   0x01U
/* MACON1 */
#define MACON1_TXPAUS  0x08U
#define MACON1_RXPAUS  0x04U
#define MACON1_PASSALL 0x02U
#define MACON1_MARXEN  0x01U
/* MACON3 */
#define MACON3_PADCFG0 0x20U     /* pad short frames to 60 bytes, append CRC */
#define MACON3_TXCRCEN 0x10U
#define MACON3_FRMLNEN 0x02U
#define MACON3_FULDPX  0x01U
/* MACON4 */
#define MACON4_DEFER   0x40U
/* MICMD */
#define MICMD_MIIRD    0x01U
/* MISTAT */
#define MISTAT_BUSY    0x01U
/* ERXFCON */
#define ERXFCON_UCEN   0x80U
#define ERXFCON_ANDOR  0x40U
#define ERXFCON_CRCEN  0x20U
#define ERXFCON_MCEN   0x02U
#define ERXFCON_BCEN   0x01U

/* ---- PHY registers (via MII) ------------------------------------------------ */
#define PHCON1     0x00U
#define PHSTAT1    0x01U
#define PHID1      0x02U
#define PHID2      0x03U
#define PHCON2     0x10U
#define PHSTAT2    0x11U
#define PHIE       0x12U
#define PHIR       0x13U
#define PHLCON     0x14U

#define PHCON1_PDPXMD   0x0100U
#define PHSTAT1_LLSTAT  0x0004U
#define PHCON2_HDLDIS   0x0100U
#define PHSTAT2_LSTAT   0x0400U
#define PHIE_PLNKIE     0x0010U
#define PHIE_PGEIE      0x0002U
#define PHID1_VALUE     0x0083U

/* ---- EREVID values ---------------------------------------------------------- */
#define EREVID_B1  0x02U
#define EREVID_B4  0x04U
#define EREVID_B5  0x05U
#define EREVID_B7  0x06U

/* ---- RX status vector (6-byte header in front of every received frame) ------ */
#define RSV_RXOK          (1UL << 23)
#define RSV_LEN_OOR       (1UL << 22)
#define RSV_LEN_CHK_ERR   (1UL << 21)
#define RSV_CRC_ERR       (1UL << 20)
#define RSV_LONG_DROP     (1UL << 16)

/* ---- TX status vector (7 bytes written at ETXND + 1) ------------------------ */
#define TSV_LATE_COLLISION      (1ULL << 29)
#define TSV_EXCESSIVE_COLLISION (1ULL << 28)
#define TSV_DONE_OK             (1ULL << 23)

#endif /* ENC28J60_REGS_H */
