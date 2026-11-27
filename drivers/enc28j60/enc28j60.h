/*
 * ENC28J60 10BASE-T Ethernet controller on the shared /SPI2/ bus of the IO layer
 * (chip select IO_SPI2_CS_ENC28J60, PB12; the bus mutex is the "lock" below).
 *
 * Receive (bus locked from rx_begin to rx_end, so read the frame in one go):
 *     uint16_t len;
 *     if (enc28j60_rx_begin(&len) == ENC_OK) { enc28j60_rx_read(p, len); enc28j60_rx_end(); }
 *
 * Transmit (bus locked from tx_begin to tx_end):
 *     enc28j60_tx_begin(); enc28j60_tx_write(part1, n1); ...; enc28j60_tx_end();
 *
 * Interrupt: the INT pin (EXTI4) only wakes the task given to enc28j60_start().
 * That task calls enc28j60_service() to learn what happened.
 */
#ifndef ENC28J60_H
#define ENC28J60_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

typedef enum {
    ENC_OK = 0,
    ENC_ERR_SPI,        /* bus lock or transfer failed */
    ENC_ERR_TIMEOUT,    /* CLKRDY, MII busy or TX did not complete */
    ENC_ERR_NO_CHIP,    /* EREVID / PHID1 do not look like an ENC28J60 */
    ENC_ERR_PARAM,
    ENC_ERR_EMPTY,      /* no frame waiting */
    ENC_ERR_RX,         /* frame dropped (bad status or corrupt header) */
    ENC_ERR_TX          /* aborted, excessive or repeated late collisions */
} enc_status_t;

/* events returned by enc28j60_service() */
#define ENC_EVT_RX      0x01U   /* EPKTCNT > 0 */
#define ENC_EVT_LINK    0x02U   /* link changed: read enc28j60_link_up() */
#define ENC_EVT_RX_ERR  0x04U   /* RX FIFO overflow */

typedef struct {
    uint32_t rx_frames;
    uint32_t rx_dropped;
    uint32_t rx_overflows;
    uint32_t rx_resets;
    uint32_t tx_frames;
    uint32_t tx_errors;
    uint32_t tx_retries;
    uint32_t tx_collisions;
    uint32_t irqs;
} enc_stats_t;

#define ENC_MAX_FRAME        1518U   /* without CRC: 1514 */
#define ENC_LOCK_TIMEOUT_MS  50U

/* Hardware + soft reset, revision check, buffers, filters, MAC and PHY (half duplex). */
enc_status_t enc28j60_init(const uint8_t mac[6]);
/* Enable RX and the INT pin; INT notifies notify_task (may be NULL for polling). */
enc_status_t enc28j60_start(TaskHandle_t notify_task);

uint8_t      enc28j60_revision(void);                 /* raw EREVID, 0 if no chip */
const char  *enc28j60_revision_name(uint8_t rev);     /* "B1", "B4", "B5", "B7" or "?" */
bool         enc28j60_link_up(void);                  /* PHSTAT2.LSTAT */
uint32_t     enc28j60_service(void);                  /* ENC_EVT_* bits */
void         enc28j60_get_stats(enc_stats_t *out);

enc_status_t enc28j60_rx_begin(uint16_t *len);
void         enc28j60_rx_read(uint8_t *dst, uint16_t n);
void         enc28j60_rx_end(void);
uint16_t     enc28j60_receive(uint8_t *buf, uint16_t size);   /* 0 = nothing / dropped */

enc_status_t enc28j60_tx_begin(void);
void         enc28j60_tx_write(const uint8_t *src, uint16_t n);
enc_status_t enc28j60_tx_end(void);
enc_status_t enc28j60_send(const uint8_t *frame, uint16_t len);

/* Called from the EXTI4 handler. */
void         enc28j60_int_isr(void);

#endif /* ENC28J60_H */
