/*
 * lwIP network interface for the ENC28J60.
 */
#ifndef ETHERNETIF_ENC28J60_H
#define ETHERNETIF_ENC28J60_H

#include <stdint.h>
#include "lwip/err.h"
#include "lwip/netif.h"

typedef struct {
    uint32_t rx_pbufs;          /* frames handed to lwIP */
    uint32_t rx_no_pbuf;        /* frames dropped: pool empty */
    uint32_t rx_input_err;      /* frames dropped: tcpip mailbox full */
    uint32_t tx_err;
} ethernetif_stats_t;

/* netif_add() init callback. Resets and configures the chip, starts the RX
   thread. Returns ERR_IF if no ENC28J60 answers. Call from the tcpip thread. */
err_t ethernetif_enc28j60_init(struct netif *netif);

void ethernetif_enc28j60_get_stats(ethernetif_stats_t *out);

#endif /* ETHERNETIF_ENC28J60_H */
