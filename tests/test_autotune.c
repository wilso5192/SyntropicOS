#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/control/syn_autotune.h"
#include "syntropic/motor/syn_motor_ctrl.h"
#include "syntropic/motor/syn_dc_motor.h"
#include "syntropic/log/syn_datalog.h"
#include <string.h>

static int32_t mock_pos = 0;
static int32_t last_mock_output = 0;

int32_t mock_read_pos(void *ctx) { (void)ctx; return mock_pos; }

void mock_set_output(void *ctx, int32_t output) {
    (void)ctx;
    last_mock_output = output;
}

void test_autotune_probe_phase(void) {
    mock_tick_ms = 0;
    mock_pos = 0;
    last_mock_output = 0;

    SYN_MotorCtrl ctrl;
    SYN_MotorCtrl_Config mcfg = SYN_MOTOR_CTRL_DEFAULTS(
        ((SYN_MotorOutput){ .set_output = mock_set_output, .ctx = NULL }),
        mock_read_pos, NULL, 1000, 100
    );
    syn_motor_ctrl_init(&ctrl, &mcfg);

    SYN_AutoTune at;
    SYN_AutoTune_Limits limits = { .position_min = -1000, .position_max = 1000 };
    syn_autotune_start(&at, &ctrl, &limits, SYN_ATUNE_FLAG_ALL, 100);

    TEST_ASSERT_EQUAL(SYN_ATUNE_PROBE, at.state);
    
    /* Initially probing at 5% */
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(5, last_mock_output);

    /* No motion for 300ms -> increment to 6% */
    mock_tick_advance(301);
    syn_autotune_update(&at); /* This increments but applies old */
    syn_autotune_update(&at); /* This applies new */
    TEST_ASSERT_EQUAL(6, last_mock_output);

    /* Advance another 300ms -> increment to 7% */
    mock_tick_advance(301);
    syn_autotune_update(&at);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(7, last_mock_output);

    /* Simulate motion at 7% */
    mock_pos = 100;
    syn_autotune_update(&at); /* transitions to BRAKING */
    syn_autotune_update(&at); /* motor stopped -> transitions to RAMP_UP */
    
    /* Should transition to RAMP_UP */
    TEST_ASSERT_EQUAL(SYN_ATUNE_RAMP_UP, at.state);
    TEST_ASSERT_EQUAL(0, last_mock_output);
    
    /* Verify test_output was computed (7 * 1.2 = 8) */
    TEST_ASSERT_EQUAL(8, at.cfg.test_output);
}

void test_autotune_datalog(void) {
    mock_tick_ms = 0;
    mock_pos = 0;
    last_mock_output = 0;

    SYN_MotorCtrl ctrl;
    SYN_MotorCtrl_Config mcfg = SYN_MOTOR_CTRL_DEFAULTS(
        ((SYN_MotorOutput){ .set_output = mock_set_output, .ctx = NULL }),
        mock_read_pos, NULL, 1000, 100
    );
    syn_motor_ctrl_init(&ctrl, &mcfg);

    uint8_t log_buf[256];
    SYN_DataLog log;
    syn_datalog_init(&log, log_buf, sizeof(log_buf));

    SYN_AutoTune at;
    SYN_AutoTune_Config cfg = {
        .mode = SYN_ATUNE_MODE_FF_IDENT,
        .test_output = 20,
        .limits = { .position_min = -1000, .position_max = 1000 },
        .datalog = &log
    };
    syn_autotune_init(&at, &ctrl, &cfg);

    syn_autotune_update(&at);

    /* Verify log entry */
    uint16_t id;
    SYN_AutoTune_LogFrame frame;
    size_t n = syn_datalog_read(&log, &id, &frame, sizeof(frame));
    TEST_ASSERT_EQUAL(sizeof(frame), n);
    TEST_ASSERT_EQUAL(SYN_ATUNE_LOG_ID, id);
    TEST_ASSERT_EQUAL(SYN_ATUNE_RAMP_UP, frame.state);
}

void test_autotune_safety_limits(void) {
    mock_tick_ms = 0;
    mock_pos = 0;
    last_mock_output = 0;

    SYN_MotorCtrl ctrl;
    SYN_MotorCtrl_Config mcfg = SYN_MOTOR_CTRL_DEFAULTS(
        ((SYN_MotorOutput){ .set_output = mock_set_output, .ctx = NULL }),
        mock_read_pos, NULL, 1000, 100
    );
    syn_motor_ctrl_init(&ctrl, &mcfg);

    SYN_AutoTune at;
    SYN_AutoTune_Limits limits = { .position_min = -10, .position_max = 10 };
    syn_autotune_start(&at, &ctrl, &limits, SYN_ATUNE_FLAG_ALL, 100);

    /* Trigger probe motion that exceeds limits */
    mock_pos = 100; 
    syn_autotune_update(&at);

    TEST_ASSERT_EQUAL(SYN_ATUNE_ABORTED, at.state);
    TEST_ASSERT_EQUAL(SYN_ATUNE_ABORT_POSITION, syn_autotune_abort_reason(&at));
}

void test_autotune_auto_sequence(void) {
    mock_tick_ms = 0;
    mock_pos = 0;
    last_mock_output = 0;

    SYN_MotorCtrl ctrl;
    SYN_MotorCtrl_Config mcfg = SYN_MOTOR_CTRL_DEFAULTS(
        ((SYN_MotorOutput){ .set_output = mock_set_output, .ctx = NULL }),
        mock_read_pos, NULL, 1000, 100
    );
    syn_motor_ctrl_init(&ctrl, &mcfg);

    SYN_AutoTune at;
    SYN_AutoTune_Limits limits = { 
        .position_min = -10000, 
        .position_max = 10000,
        .watchdog_ms = 10000 
    };
    syn_autotune_start(&at, &ctrl, &limits, SYN_ATUNE_FLAG_ALL, 100);

    /* 1. Probe phase */
    mock_pos = 100;
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_BRAKING, at.state);
    
    /* 2. Transition to FF Identification */
    mock_tick_advance(2001);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_RAMP_UP, at.state);
    
    /* Ramp up completes */
    mock_tick_advance(501);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_SETTLING, at.state);
    
    /* Settling completes (adaptive) */
    for (int i = 0; i < 4; i++) {
        mock_tick_advance(300);
        syn_autotune_update(&at);
    }
    /* P2 should be captured by now (1200ms elapsed) */
    for (int i = 0; i < 4; i++) {
        mock_tick_advance(300);
        syn_autotune_update(&at);
    }
    /* Steady-state should be reached (another 1200ms elapsed) */
    TEST_ASSERT_EQUAL(SYN_ATUNE_MEASURING, at.state);
    
    /* Measuring 1 completes -> transition to SETTLING_2 */
    mock_tick_advance(2001);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_SETTLING_2, at.state);

    /* Settling 2 completes -> transition to MEASURING_2 */
    mock_tick_advance(1001);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_MEASURING_2, at.state);

    /* Measuring 2 completes -> transition to BRAKING */
    mock_pos = 2000; /* faster velocity */
    mock_tick_advance(2001);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_BRAKING, at.state);
    TEST_ASSERT_TRUE(at.result.ff_kv > 0);

    /* 3. Transition to Relay Tuning */
    mock_tick_advance(2001);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_RAMP_UP, at.state);
    
    mock_tick_advance(501);
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_RELAY, at.state);
}

void test_autotune_edge_cases(void) {
    mock_tick_ms = 0;
    mock_pos = 0;
    last_mock_output = 0;

    SYN_MotorCtrl ctrl;
    SYN_MotorCtrl_Config mcfg = SYN_MOTOR_CTRL_DEFAULTS(
        ((SYN_MotorOutput){ .set_output = mock_set_output, .ctx = NULL }),
        mock_read_pos, NULL, 1000, 100
    );
    syn_motor_ctrl_init(&ctrl, &mcfg);

    SYN_AutoTune at;
    SYN_AutoTune_Limits limits = {0};
    syn_autotune_start(&at, &ctrl, &limits, SYN_ATUNE_FLAG_TUNE_PID, 100);
    
    /* 1. Trigger zero amplitude branch (line 456) */
    at.state = SYN_ATUNE_RELAY;
    at.period_count = at.cfg.relay_cycles;
    at.amplitude_count = 0; /* Force zero */
    syn_autotune_update(&at);
    TEST_ASSERT_EQUAL(SYN_ATUNE_RAMP_DOWN, at.state);
    
    /* 2. Trigger scale increment (line 475) */
    syn_autotune_start(&at, &ctrl, &limits, SYN_ATUNE_FLAG_TUNE_PID, 100);
    at.cfg.test_output = 1;
    at.state = SYN_ATUNE_RELAY;
    at.period_count = at.cfg.relay_cycles;
    at.amplitude_sum = 2000;
    at.amplitude_count = 1; /* half_amp = 1000 */
    syn_autotune_update(&at);
    TEST_ASSERT_TRUE(at.result.pid_scale > 8);
}

void test_autotune_gain_multiplier(void) {
    mock_tick_ms = 0;
    mock_pos = 0;
    last_mock_output = 0;

    SYN_MotorCtrl ctrl;
    SYN_MotorCtrl_Config mcfg = SYN_MOTOR_CTRL_DEFAULTS(
        ((SYN_MotorOutput){ .set_output = mock_set_output, .ctx = NULL }),
        mock_read_pos, NULL, 1000, 100
    );
    syn_motor_ctrl_init(&ctrl, &mcfg);

    SYN_AutoTune at;
    SYN_AutoTune_Limits limits = {0};
    
    /* Tune at 100% */
    syn_autotune_start(&at, &ctrl, &limits, SYN_ATUNE_FLAG_TUNE_PID, 100);
    at.state = SYN_ATUNE_RELAY;
    at.period_count = at.cfg.relay_cycles;
    at.amplitude_sum = 2000;
    at.amplitude_count = 1;
    syn_autotune_update(&at);
    int32_t kp_100 = at.result.kp;
    
    /* Tune at 50% */
    syn_autotune_start(&at, &ctrl, &limits, SYN_ATUNE_FLAG_TUNE_PID, 50);
    at.state = SYN_ATUNE_RELAY;
    at.period_count = at.cfg.relay_cycles;
    at.amplitude_sum = 2000;
    at.amplitude_count = 1;
    syn_autotune_update(&at);
    int32_t kp_50 = at.result.kp;
    
    TEST_ASSERT_EQUAL(kp_100 / 2, kp_50);
}

void run_autotune_tests(void) {
    RUN_TEST(test_autotune_probe_phase);
    RUN_TEST(test_autotune_datalog);
    RUN_TEST(test_autotune_safety_limits);
    RUN_TEST(test_autotune_auto_sequence);
    RUN_TEST(test_autotune_edge_cases);
    RUN_TEST(test_autotune_gain_multiplier);
}
