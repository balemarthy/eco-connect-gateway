/*
 * lwIP netif glue for the ENC28J60.
 *
 *   EXTI4 (INT pin) --notify--> enc_rx thread --pbuf--> tcpip_input() --> tcpip thread
 *   tcpip thread --linkoutput--> low_level_output() --SPI--> ENC28J60 TX buffer
 *
 * The RX thread also wakes every NET_RX_POLL_MS: EPKTCNT is the source of truth
 * (PKTIF is unreliable), so a missed edge costs latency, never a stuck FIFO.
 * Link changes from the PHY are passed to lwIP under the core lock.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#define LOG_TAG LOG_MOD_NET
#include "log.h"

#include "board.h"
#include "enc28j60.h"
#include "ethernetif_enc28j60.h"
#include "supervisor.h"

#define NET_RX_STACK_WORDS  512U
#define NET_RX_PRIORITY     (tskIDLE_PRIORITY + 5U)    /* above tcpip (4): drain the chip FIFO first */
#define NET_RX_POLL_MS      50U

static StaticTask_t rx_tcb;
static StackType_t  rx_stack[NET_RX_STACK_WORDS];
static ethernetif_stats_t stats;

/* ---------------------------------------------------------------- TX (tcpip thread) */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (enc28j60_tx_begin() != ENC_OK) {
        stats.tx_err++;
        return ERR_IF;
    }
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        enc28j60_tx_write((const uint8_t *)q->payload, q->len);
    }
    if (enc28j60_tx_end() != ENC_OK) {
        stats.tx_err++;
        return ERR_IF;
    }
    return ERR_OK;
}

/* ---------------------------------------------------------------- RX (enc_rx thread) */

/* Returns false when the chip has no more frames. */
static bool low_level_input(struct netif *netif)
{
    uint16_t len = 0U;
    enc_status_t st = enc28j60_rx_begin(&len);
    if (st == ENC_ERR_EMPTY || st == ENC_ERR_SPI || st == ENC_ERR_NO_CHIP) {
        return false;
    }
    if (st != ENC_OK) {
        return true;                      /* bad frame dropped by the driver, try the next */
    }

    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p != NULL) {
        for (struct pbuf *q = p; q != NULL; q = q->next) {
            enc28j60_rx_read((uint8_t *)q->payload, q->len);
        }
    } else {
        stats.rx_no_pbuf++;
    }
    enc28j60_rx_end();                    /* frees the frame in the chip either way */

    if (p != NULL) {
        if (netif->input(p, netif) != ERR_OK) {
            stats.rx_input_err++;
            pbuf_free(p);
        } else {
            stats.rx_pbufs++;
        }
    }
    return true;
}

static void set_link(struct netif *netif, bool up)
{
    LOCK_TCPIP_CORE();
    if (up) {
        netif_set_link_up(netif);
    } else {
        netif_set_link_down(netif);
    }
    UNLOCK_TCPIP_CORE();
}

static void rx_thread(void *arg)
{
    struct netif *netif = (struct netif *)arg;
    bool link = enc28j60_link_up();

    sup_id_t sup_id = sup_register("net", 2000U);
    set_link(netif, link);
    (void)enc28j60_start(xTaskGetCurrentTaskHandle());

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NET_RX_POLL_MS));
        sup_checkin(sup_id);

        uint32_t ev = enc28j60_service();
        if ((ev & ENC_EVT_LINK) != 0U) {
            bool now = enc28j60_link_up();
            if (now != link) {
                link = now;
                set_link(netif, link);
            }
        }
        if ((ev & ENC_EVT_RX_ERR) != 0U) {
            LOG_W("rx overflow");
        }
        /* drain everything that is waiting, even without an RX event (polling fallback) */
        while (low_level_input(netif)) {
        }
    }
}

/* ---------------------------------------------------------------- netif init */

err_t ethernetif_enc28j60_init(struct netif *netif)
{
    uint8_t mac[6];
    board_mac_address(mac);

    if (enc28j60_init(mac) != ENC_OK) {
        return ERR_IF;
    }

#if LWIP_NETIF_HOSTNAME
    netif->hostname = "eco-connect";
#endif
    netif->name[0]    = 'e';
    netif->name[1]    = 'n';
    netif->output     = etharp_output;
    netif->linkoutput = low_level_output;
    netif->mtu        = 1500U;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, mac, ETH_HWADDR_LEN);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;

    TaskHandle_t h = xTaskCreateStatic(rx_thread, "enc_rx", NET_RX_STACK_WORDS, netif, NET_RX_PRIORITY,
                                       rx_stack, &rx_tcb);
    configASSERT(h != NULL);
    return ERR_OK;
}

void ethernetif_enc28j60_get_stats(ethernetif_stats_t *out)
{
    *out = stats;
}
