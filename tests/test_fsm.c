/**
 * @file test_fsm.c
 * @brief Unity tests for syn_fsm.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/util/syn_fsm.h"

/* Log now writes directly to mock serial */
#define log_capture_buf  ((char *)mock_serial_tx_buf)
#define log_capture_pos  mock_serial_tx_len

enum { FSM_ST_IDLE, FSM_ST_RUNNING, FSM_ST_ERROR };
enum { FSM_EV_START, FSM_EV_STOP, FSM_EV_FAULT };

static int fsm_action_called = 0;
static void fsm_on_start(void *ctx) { (void)ctx; fsm_action_called = 1; }
static void fsm_on_stop(void *ctx)  { (void)ctx; fsm_action_called = 2; }

static bool fsm_guard_deny(void *ctx) { (void)ctx; return false; }

static const SYN_FSM_Transition test_fsm_table[] = {
    { FSM_ST_IDLE,    FSM_EV_START, FSM_ST_RUNNING, NULL,           fsm_on_start },
    { FSM_ST_RUNNING, FSM_EV_STOP,  FSM_ST_IDLE,    NULL,           fsm_on_stop  },
    { FSM_ST_RUNNING, FSM_EV_FAULT, FSM_ST_ERROR,   NULL,           NULL         },
    { FSM_ST_IDLE,    FSM_EV_FAULT, FSM_ST_IDLE,    fsm_guard_deny, NULL         },
    SYN_FSM_END
};

static void test_fsm(void)
{

    /* Re-init log so FSM can log transitions */
    log_capture_pos = 0;
    syn_log_init(SYN_LOG_DEBUG);

    SYN_FSM fsm;
    syn_fsm_init(&fsm, test_fsm_table, FSM_ST_IDLE, "fsm");

    TEST_ASSERT_EQUAL(FSM_ST_IDLE, syn_fsm_state(&fsm));
    TEST_ASSERT_TRUE(syn_fsm_in_state(&fsm, FSM_ST_IDLE));

    /* Dispatch START */
    fsm_action_called = 0;
    bool took = syn_fsm_dispatch(&fsm, FSM_EV_START);
    TEST_ASSERT_TRUE(took);
    TEST_ASSERT_EQUAL(FSM_ST_RUNNING, syn_fsm_state(&fsm));
    TEST_ASSERT_EQUAL_INT(1, fsm_action_called);

    /* Dispatch STOP */
    took = syn_fsm_dispatch(&fsm, FSM_EV_STOP);
    TEST_ASSERT_TRUE(took);
    TEST_ASSERT_EQUAL(FSM_ST_IDLE, syn_fsm_state(&fsm));
    TEST_ASSERT_EQUAL_INT(2, fsm_action_called);

    /* Guard blocks transition */
    took = syn_fsm_dispatch(&fsm, FSM_EV_FAULT);
    TEST_ASSERT_FALSE(took);
    TEST_ASSERT_EQUAL(FSM_ST_IDLE, syn_fsm_state(&fsm));

    /* No matching transition */
    took = syn_fsm_dispatch(&fsm, FSM_EV_STOP);
    TEST_ASSERT_FALSE(took);

    /* Force state */
    syn_fsm_set_state(&fsm, FSM_ST_ERROR);
    TEST_ASSERT_EQUAL(FSM_ST_ERROR, syn_fsm_state(&fsm));
}

static int enter_idle_count = 0;
static int exit_idle_count = 0;
static int enter_run_count = 0;
static int exit_run_count = 0;

static void on_enter_idle(void *ctx) { (void)ctx; enter_idle_count++; }
static void on_exit_idle(void *ctx)  { (void)ctx; exit_idle_count++; }
static void on_enter_run(void *ctx)  { (void)ctx; enter_run_count++; }
static void on_exit_run(void *ctx)   { (void)ctx; exit_run_count++; }

static bool guard_allow(void *ctx) { (void)ctx; return true; }

static const SYN_FSM_Transition test_fsm_edge_table[] = {
    { FSM_ST_IDLE,    FSM_EV_START, FSM_ST_RUNNING, guard_allow,    NULL },
    { FSM_ST_RUNNING, FSM_EV_STOP,  FSM_ST_IDLE,    NULL,           NULL },
    SYN_FSM_END
};

static const SYN_FSM_StateDesc test_fsm_descs[] = {
    { FSM_ST_IDLE,    on_enter_idle, on_exit_idle },
    { FSM_ST_RUNNING, on_enter_run,  on_exit_run  },
    SYN_FSM_STATE_END
};

static const char *const test_state_names[] = {
    "IDLE", "RUNNING", "ERROR"
};

static void test_fsm_edge_cases(void)
{
    SYN_FSM fsm;
    syn_fsm_init(&fsm, test_fsm_edge_table, FSM_ST_IDLE, "fsm_edge");

    /* Register context */
    int context_val = 42;
    syn_fsm_set_context(&fsm, &context_val);

    /* Register descriptors */
    syn_fsm_set_state_descs(&fsm, test_fsm_descs);

    /* Register state names */
    syn_fsm_set_state_names(&fsm, test_state_names);

    /* Verify initial state and callbacks reset */
    enter_idle_count = 0;
    exit_idle_count  = 0;
    enter_run_count  = 0;
    exit_run_count   = 0;

    /* Transition with guard_allow (returns true) */
    log_capture_pos = 0;
    syn_log_init(SYN_LOG_DEBUG);

    bool took = syn_fsm_dispatch(&fsm, FSM_EV_START);
    TEST_ASSERT_TRUE(took);
    TEST_ASSERT_EQUAL(FSM_ST_RUNNING, syn_fsm_state(&fsm));

    /* Verify callbacks fired */
    TEST_ASSERT_EQUAL_INT(1, exit_idle_count);
    TEST_ASSERT_EQUAL_INT(1, enter_run_count);

    /* Verify log contains state names: "IDLE -> RUNNING" */
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "IDLE -> RUNNING"));

    /* Set state to current state (RUNNING -> RUNNING) should not trigger entry/exit */
    enter_run_count = 0;
    exit_run_count  = 0;
    syn_fsm_set_state(&fsm, FSM_ST_RUNNING);
    TEST_ASSERT_EQUAL_INT(0, enter_run_count);
    TEST_ASSERT_EQUAL_INT(0, exit_run_count);

    /* Set state to a different state (RUNNING -> IDLE) manually */
    syn_fsm_set_state(&fsm, FSM_ST_IDLE);
    TEST_ASSERT_EQUAL_INT(1, exit_run_count);
    TEST_ASSERT_EQUAL_INT(1, enter_idle_count);

    /* Set state manually to FSM_ST_ERROR (not in descs) to hit return NULL */
    syn_fsm_set_state(&fsm, FSM_ST_ERROR);
    TEST_ASSERT_EQUAL(FSM_ST_ERROR, syn_fsm_state(&fsm));

    /* Transition with NULL tag (verify no log output and no crash) */
    SYN_FSM fsm_no_log;
    syn_fsm_init(&fsm_no_log, test_fsm_edge_table, FSM_ST_IDLE, NULL); // tag = NULL
    
    log_capture_pos = 0;
    took = syn_fsm_dispatch(&fsm_no_log, FSM_EV_START);
    TEST_ASSERT_TRUE(took);
    TEST_ASSERT_EQUAL_INT(0, (int)log_capture_pos); // log is empty
}

void run_fsm_tests(void)
{
    RUN_TEST(test_fsm);
    RUN_TEST(test_fsm_edge_cases);
}
