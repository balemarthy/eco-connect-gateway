/*
 * Host tests: gateway state machine.
 */
#include "gateway_fsm.h"
#include "test_util.h"

static void boot_to_reporting(void)
{
    gw_fsm_t f;
    gw_fsm_init(&f);
    CHECK_EQ_INT(f.state, GW_IDLE);
    CHECK(gw_fsm_apply(&f, GW_EV_READING_OK));
    CHECK_EQ_INT(f.state, GW_SAMPLING);
    CHECK(gw_fsm_apply(&f, GW_EV_LINK_UP));
    CHECK_EQ_INT(f.state, GW_CONNECTED);
    CHECK(gw_fsm_apply(&f, GW_EV_REPORT));
    CHECK_EQ_INT(f.state, GW_REPORTING);
    CHECK(!gw_fsm_apply(&f, GW_EV_REPORT));            /* no change, no log line */
    CHECK(gw_fsm_apply(&f, GW_EV_REPORT_IDLE));
    CHECK_EQ_INT(f.state, GW_CONNECTED);
}

static void link_before_reading(void)
{
    gw_fsm_t f;
    gw_fsm_init(&f);
    CHECK(!gw_fsm_apply(&f, GW_EV_LINK_UP));           /* still IDLE: nothing to report */
    CHECK_EQ_INT(f.state, GW_IDLE);
    CHECK(!gw_fsm_apply(&f, GW_EV_REPORT));            /* request before any reading */
    CHECK_EQ_INT(f.state, GW_IDLE);
    CHECK(gw_fsm_apply(&f, GW_EV_READING_OK));
    CHECK_EQ_INT(f.state, GW_CONNECTED);               /* straight past SAMPLING */
}

static void link_loss(void)
{
    gw_fsm_t f;
    gw_fsm_init(&f);
    (void)gw_fsm_apply(&f, GW_EV_READING_OK);
    (void)gw_fsm_apply(&f, GW_EV_LINK_UP);
    (void)gw_fsm_apply(&f, GW_EV_REPORT);
    CHECK(gw_fsm_apply(&f, GW_EV_LINK_DOWN));
    CHECK_EQ_INT(f.state, GW_SAMPLING);
    CHECK(gw_fsm_apply(&f, GW_EV_LINK_UP));
    CHECK_EQ_INT(f.state, GW_CONNECTED);               /* reporting was cleared by the loss */
}

static void sensor_fault_and_recovery(void)
{
    gw_fsm_t f;
    gw_fsm_init(&f);
    (void)gw_fsm_apply(&f, GW_EV_READING_OK);
    (void)gw_fsm_apply(&f, GW_EV_LINK_UP);
    CHECK(!gw_fsm_apply(&f, GW_EV_READING_FAIL));
    CHECK(!gw_fsm_apply(&f, GW_EV_READING_FAIL));
    CHECK_EQ_INT(f.state, GW_CONNECTED);               /* below the limit */
    CHECK(gw_fsm_apply(&f, GW_EV_READING_FAIL));
    CHECK_EQ_INT(f.state, GW_FAULT);
    CHECK(!gw_fsm_apply(&f, GW_EV_LINK_UP));
    CHECK(gw_fsm_apply(&f, GW_EV_READING_OK));         /* sensor-pull recovery */
    CHECK_EQ_INT(f.state, GW_CONNECTED);
    CHECK(!gw_fsm_apply(&f, GW_EV_READING_FAIL));      /* the run restarted at 0 */
    for (int i = 0; i < 300; i++) {
        (void)gw_fsm_apply(&f, GW_EV_READING_FAIL);    /* counter must not wrap to 0 */
    }
    CHECK_EQ_INT(f.state, GW_FAULT);
}

static void failure_before_first_reading(void)
{
    gw_fsm_t f;
    gw_fsm_init(&f);
    (void)gw_fsm_apply(&f, GW_EV_READING_FAIL);
    (void)gw_fsm_apply(&f, GW_EV_READING_FAIL);
    CHECK_EQ_INT(f.state, GW_IDLE);
    CHECK(gw_fsm_apply(&f, GW_EV_READING_FAIL));
    CHECK_EQ_INT(f.state, GW_FAULT);                   /* sensor never worked */
}

static void stall_is_sticky(void)
{
    gw_fsm_t f;
    gw_fsm_init(&f);
    (void)gw_fsm_apply(&f, GW_EV_READING_OK);
    CHECK(gw_fsm_apply(&f, GW_EV_TASK_STALL));
    CHECK_EQ_INT(f.state, GW_FAULT);
    for (int e = 0; e < GW_EV_COUNT; e++) {
        (void)gw_fsm_apply(&f, (gw_event_t)e);
        CHECK_EQ_INT(f.state, GW_FAULT);
    }
}

static void names(void)
{
    CHECK_EQ_STR(gw_state_name(GW_REPORTING), "REPORTING");
    CHECK_EQ_STR(gw_state_name(GW_STATE_COUNT), "?");
    CHECK_EQ_STR(gw_event_name(GW_EV_TASK_STALL), "task_stall");
    gw_fsm_t f;
    gw_fsm_init(&f);
    CHECK(!gw_fsm_apply(&f, GW_EV_COUNT));             /* unknown event ignored */
}

int main(void)
{
    RUN(boot_to_reporting);
    RUN(link_before_reading);
    RUN(link_loss);
    RUN(sensor_fault_and_recovery);
    RUN(failure_before_first_reading);
    RUN(stall_is_sticky);
    RUN(names);
    TEST_SUMMARY("test_fsm");
}
