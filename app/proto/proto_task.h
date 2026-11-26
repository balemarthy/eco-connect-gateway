/*
 * Protocol task: USART1 bytes -> frame decoder -> command handler -> response frame.
 */
#ifndef PROTO_TASK_H
#define PROTO_TASK_H

#include <stdint.h>

void proto_start(void);

typedef struct {
    uint32_t frames_ok;
    uint32_t bad_cs;
    uint32_t bad_packet;
    uint32_t overflow;
} proto_stats_t;

void proto_get_stats(proto_stats_t *out);

#endif /* PROTO_TASK_H */
