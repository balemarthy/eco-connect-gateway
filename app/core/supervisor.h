/*
 * Supervisor: task check-ins, watchdog feeding, gateway state machine.
 *
 * Every long-running task registers once with the longest silence it is allowed
 * and calls sup_checkin() from its loop. The supervisor feeds the IWDG only while
 * every registered task (and the lwIP tcpip thread, probed with a callback) is
 * inside its window. One late task: "[E][sup] task <name> missed", the state
 * machine goes to FAULT, feeding stops and the IWDG resets the board.
 *
 * The supervisor runs at the lowest application priority: if it cannot get the
 * CPU for a whole watchdog period, the system is overloaded and a reset is the
 * right answer too.
 */
#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <stdint.h>

#include "gateway_fsm.h"

typedef int8_t sup_id_t;

#define SUP_INVALID         ((sup_id_t)-1)
#define SUP_MAX_TASKS       12U
#define SUP_PERIOD_MS       250U
#define SUP_IWDG_TIMEOUT_MS 2000U

#ifndef SUP_USE_IWDG
#define SUP_USE_IWDG 1          /* make WATCHDOG=0 to build without the IWDG */
#endif

/* Create the supervisor task and the event queue. Call first, before the other *_start(). */
void sup_start(void);

/* Any time, any task. Returns SUP_INVALID when the table is full. */
sup_id_t sup_register(const char *name, uint32_t window_ms);
void     sup_checkin(sup_id_t id);

/* Non-blocking event for the state machine (dropped if the queue is full). */
void       gw_post(gw_event_t ev);
gw_state_t gw_state(void);

#endif /* SUPERVISOR_H */
