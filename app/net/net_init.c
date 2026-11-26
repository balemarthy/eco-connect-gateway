/*
 * Network bring-up: lwIP core, its tcpip thread and the ENC28J60 netif.
 */
#include <string.h>

#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"

#define LOG_TAG LOG_MOD_NET
#include "log.h"

#include "ethernetif_enc28j60.h"
#include "net_init.h"

static struct netif enc_netif;
static volatile bool stack_ready;
static volatile bool netif_ok;

static void netif_status_changed(struct netif *n)
{
    if (netif_is_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n))) {
        char ip[16];
        net_ip_string(ip, sizeof ip);
        LOG_I("netif up %s", ip);
    } else {
        LOG_I("netif down");
    }
}

static void netif_link_changed(struct netif *n)
{
    LOG_I("link %s", netif_is_link_up(n) ? "up" : "down");
}

/* Runs in the tcpip thread (core lock held) once it is up. */
static void tcpip_ready(void *arg)
{
    (void)arg;
    ip4_addr_t ip, mask, gw;

    stack_ready = true;
    LOG_I("tcpip thread up (lwIP %s)", LWIP_VERSION_STRING);

    (void)ip4addr_aton(NET_STATIC_IP, &ip);
    (void)ip4addr_aton(NET_STATIC_MASK, &mask);
    (void)ip4addr_aton(NET_STATIC_GW, &gw);
#if NET_USE_DHCP
    ip4_addr_set_zero(&ip);
    ip4_addr_set_zero(&mask);
    ip4_addr_set_zero(&gw);
#endif

    if (netif_add(&enc_netif, &ip, &mask, &gw, NULL, ethernetif_enc28j60_init, tcpip_input) == NULL) {
        LOG_W("no ENC28J60, network off");
        return;
    }
    netif_set_status_callback(&enc_netif, netif_status_changed);
    netif_set_link_callback(&enc_netif, netif_link_changed);
    netif_set_default(&enc_netif);
    netif_set_up(&enc_netif);
    netif_ok = true;
#if NET_USE_DHCP
    (void)dhcp_start(&enc_netif);
    LOG_I("DHCP started");
#endif
}

void net_init(void)
{
    tcpip_init(tcpip_ready, NULL);
}

bool net_stack_ready(void)
{
    return stack_ready;
}

bool net_link_up(void)
{
    return netif_ok && netif_is_link_up(&enc_netif);
}

bool net_is_up(void)
{
    return net_link_up() && netif_is_up(&enc_netif) && !ip4_addr_isany_val(*netif_ip4_addr(&enc_netif));
}

void net_ip_string(char *buf, size_t size)
{
    if (size == 0U) {
        return;
    }
    if (!netif_ok || ip4addr_ntoa_r(netif_ip4_addr(&enc_netif), buf, (int)size) == NULL) {
        strncpy(buf, "0.0.0.0", size - 1U);
        buf[size - 1U] = '\0';
    }
}
