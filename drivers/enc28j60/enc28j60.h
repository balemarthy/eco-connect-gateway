/*
 * ENC28J60 10BASE-T Ethernet controller on SPI2.
 */
#ifndef ENC28J60_H
#define ENC28J60_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENC_OK = 0,
    ENC_ERR_SPI,        /* bus lock or transfer failed */
    ENC_ERR_TIMEOUT,    /* CLKRDY or MII busy did not clear */
    ENC_ERR_NO_CHIP,    /* EREVID / PHID1 do not look like an ENC28J60 */
    ENC_ERR_PARAM
} enc_status_t;

/* Hardware + soft reset, revision check, MAC and PHY set-up (half duplex). */
enc_status_t enc28j60_init(const uint8_t mac[6]);

uint8_t      enc28j60_revision(void);                 /* raw EREVID, 0 before init */
const char  *enc28j60_revision_name(uint8_t rev);     /* "B1", "B4", "B5", "B7" or "?" */
bool         enc28j60_link_up(void);                  /* PHSTAT2.LSTAT */

#endif /* ENC28J60_H */
