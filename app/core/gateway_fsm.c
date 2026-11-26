/*
 * Gateway state machine (see gateway_fsm.h).
 */
#include "gateway_fsm.h"

static const char *const state_names[GW_STATE_COUNT] = {
    "IDLE", "SAMPLING", "CONNECTED", "REPORTING", "FAULT"
};

static const char *const event_names[GW_EV_COUNT] = {
    "reading_ok", "reading_fail", "link_up", "link_down", "report", "report_idle", "task_stall"
};

const char *gw_state_name(gw_state_t s)
{
    return ((unsigned)s < GW_STATE_COUNT) ? state_names[s] : "?";
}

const char *gw_event_name(gw_event_t e)
{
    return ((unsigned)e < GW_EV_COUNT) ? event_names[e] : "?";
}

void gw_fsm_init(gw_fsm_t *f)
{
    f->state = GW_IDLE;
    f->have_reading = false;
    f->link_up = false;
    f->reporting = false;
    f->stalled = false;
    f->fail_run = 0U;
}

static gw_state_t derive(const gw_fsm_t *f)
{
    if (f->stalled || f->fail_run >= GW_SENSOR_FAIL_LIMIT) {
        return GW_FAULT;
    }
    if (!f->have_reading) {
        return GW_IDLE;
    }
    if (!f->link_up) {
        return GW_SAMPLING;
    }
    return f->reporting ? GW_REPORTING : GW_CONNECTED;
}

bool gw_fsm_apply(gw_fsm_t *f, gw_event_t ev)
{
    switch (ev) {
    case GW_EV_READING_OK:
        f->have_reading = true;
        f->fail_run = 0U;
        break;
    case GW_EV_READING_FAIL:
        if (f->fail_run < 0xFFU) {
            f->fail_run++;
        }
        break;
    case GW_EV_LINK_UP:
        f->link_up = true;
        break;
    case GW_EV_LINK_DOWN:
        f->link_up = false;
        f->reporting = false;
        break;
    case GW_EV_REPORT:
        /* only meaningful when there is something to report over a live link */
        f->reporting = f->link_up && f->have_reading;
        break;
    case GW_EV_REPORT_IDLE:
        f->reporting = false;
        break;
    case GW_EV_TASK_STALL:
        f->stalled = true;
        break;
    default:
        return false;
    }
    gw_state_t next = derive(f);
    bool changed = (next != f->state);
    f->state = next;
    return changed;
}
