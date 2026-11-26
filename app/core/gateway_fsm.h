/*
 * Gateway state machine. Pure C (no RTOS): the supervisor task owns the instance,
 * host tests drive it directly.
 *
 * Events update a few conditions; the state follows from the conditions, in
 * priority order, so every state is reachable from every other one without a
 * special case for each pair:
 *
 *     stalled task             -> FAULT (sticky: the watchdog resets the board)
 *     N sensor failures in row -> FAULT (cleared by the next good reading)
 *     no reading yet           -> IDLE
 *     link down / no address   -> SAMPLING
 *     client served recently   -> REPORTING
 *     otherwise                -> CONNECTED
 *
 *     IDLE --reading--> SAMPLING --link up--> CONNECTED --request--> REPORTING
 *       ^                   ^  \__ link down __/  ^   \___ idle 10 s ___/
 *       |                   |                     |
 *       +---- (boot) ----  FAULT <-- 3 bad reads / task stall
 */
#ifndef GATEWAY_FSM_H
#define GATEWAY_FSM_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GW_IDLE = 0,
    GW_SAMPLING,
    GW_CONNECTED,
    GW_REPORTING,
    GW_FAULT,
    GW_STATE_COUNT
} gw_state_t;

typedef enum {
    GW_EV_READING_OK = 0,   /* sensor delivered a good value */
    GW_EV_READING_FAIL,     /* sensor read failed */
    GW_EV_LINK_UP,          /* link up and an IPv4 address */
    GW_EV_LINK_DOWN,
    GW_EV_REPORT,           /* a client got data (HTTP or serial) */
    GW_EV_REPORT_IDLE,      /* no client for GW_REPORT_IDLE_MS */
    GW_EV_TASK_STALL,       /* supervisor: a task missed its check-in */
    GW_EV_COUNT
} gw_event_t;

#define GW_SENSOR_FAIL_LIMIT  3U
#define GW_REPORT_IDLE_MS     10000U

typedef struct {
    gw_state_t state;
    bool       have_reading;
    bool       link_up;
    bool       reporting;
    bool       stalled;
    uint8_t    fail_run;      /* consecutive failed readings */
} gw_fsm_t;

void        gw_fsm_init(gw_fsm_t *f);
/* Applies one event; returns true when the state changed. */
bool        gw_fsm_apply(gw_fsm_t *f, gw_event_t ev);
const char *gw_state_name(gw_state_t s);
const char *gw_event_name(gw_event_t e);

#endif /* GATEWAY_FSM_H */
