/*
 * Network bring-up: lwIP core and its tcpip thread.
 */
#include "lwip/init.h"
#include "lwip/tcpip.h"

#define LOG_TAG LOG_MOD_NET
#include "log.h"

#include "net_init.h"

static volatile bool stack_ready;

/* Runs in the tcpip thread once it is up. */
static void tcpip_ready(void *arg)
{
    (void)arg;
    stack_ready = true;
    LOG_I("tcpip thread up (lwIP %s)", LWIP_VERSION_STRING);
}

void net_init(void)
{
    tcpip_init(tcpip_ready, NULL);
}

bool net_stack_ready(void)
{
    return stack_ready;
}
