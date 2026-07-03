/**
 * @file test_control_stats.c
 * @brief Unit tests for syn_control_stats — performance monitoring.
 *
 * Tests reset, accumulation, report generation, and edge cases.
 */

#include "unity/unity.h"
#include "syntropic/control/syn_control_stats.h"
#include <string.h>
#include <math.h>

/* ── Tests ──────────────────────────────────────────────────────────────── */

static void test_control_stats_reset(void)
{
    SYN_ControlStats stats;
    memset(&stats, 0xFF, sizeof(stats));  /* Dirty fill */

    syn_control_stats_reset(&stats);

    TEST_ASSERT_EQUAL_INT64(0, stats.sum_sq_err);
    TEST_ASSERT_EQUAL_INT64(0, stats.sum_sq_out);
    TEST_ASSERT_EQUAL_INT64(0, stats.sum_abs_delta);
    TEST_ASSERT_EQUAL_INT64(0, stats.sum_itae);
    TEST_ASSERT_EQUAL_INT32(0, stats.max_error);
    TEST_ASSERT_EQUAL_INT32(0, stats.last_output);
    TEST_ASSERT_EQUAL_UINT32(0, stats.samples);
}

static void test_control_stats_report_zero_samples(void)
{
    SYN_ControlStats stats;
    SYN_ControlReport report;

    syn_control_stats_reset(&stats);

    /* Should not crash, report should be zeroed */
    memset(&report, 0xFF, sizeof(report));
    syn_control_stats_report(&stats, &report);

    TEST_ASSERT_EQUAL_INT32(0, report.rms_error);
    TEST_ASSERT_EQUAL_INT32(0, report.max_error);
    TEST_ASSERT_EQUAL_INT32(0, report.control_effort);
    TEST_ASSERT_EQUAL_INT32(0, report.jitter);
    TEST_ASSERT_EQUAL_INT32(0, report.itae);
}

static void test_control_stats_single_sample(void)
{
    SYN_ControlStats stats;
    SYN_ControlReport report;

    syn_control_stats_reset(&stats);
    syn_control_stats_update(&stats, 10, 50);

    TEST_ASSERT_EQUAL_UINT32(1, stats.samples);
    TEST_ASSERT_EQUAL_INT32(10, stats.max_error);

    syn_control_stats_report(&stats, &report);

    /* RMS of a single value 10 = 10 */
    TEST_ASSERT_EQUAL_INT32(10, report.rms_error);
    TEST_ASSERT_EQUAL_INT32(10, report.max_error);
    /* Effort: RMS of 50 = 50, scaled by 100 = 5000 */
    TEST_ASSERT_EQUAL_INT32(5000, report.control_effort);
    /* Jitter: only 1 sample, no delta computed → 0 */
    TEST_ASSERT_EQUAL_INT32(0, report.jitter);
    /* ITAE: sample index 0 * |error| 10 = 0 */
    TEST_ASSERT_EQUAL_INT32(0, report.itae);
}

static void test_control_stats_max_error_tracking(void)
{
    SYN_ControlStats stats;
    SYN_ControlReport report;

    syn_control_stats_reset(&stats);
    syn_control_stats_update(&stats, 5, 0);
    syn_control_stats_update(&stats, -20, 0);
    syn_control_stats_update(&stats, 10, 0);

    syn_control_stats_report(&stats, &report);

    /* Max absolute error should be 20 (from -20) */
    TEST_ASSERT_EQUAL_INT32(20, report.max_error);
}

static void test_control_stats_jitter(void)
{
    SYN_ControlStats stats;
    SYN_ControlReport report;

    syn_control_stats_reset(&stats);

    /* Output sequence: 0, 10, 5, 15 */
    syn_control_stats_update(&stats, 0, 0);
    syn_control_stats_update(&stats, 0, 10);   /* delta = 10 */
    syn_control_stats_update(&stats, 0, 5);    /* delta = 5 */
    syn_control_stats_update(&stats, 0, 15);   /* delta = 10 */

    /* sum_abs_delta = 10 + 5 + 10 = 25, samples = 4 → jitter = 25/4 = 6 */
    syn_control_stats_report(&stats, &report);
    TEST_ASSERT_EQUAL_INT32(6, report.jitter);
}

static void test_control_stats_rms_error(void)
{
    SYN_ControlStats stats;
    SYN_ControlReport report;

    syn_control_stats_reset(&stats);

    /* Constant error of 3 for 4 samples → RMS = 3 */
    for (int i = 0; i < 4; i++) {
        syn_control_stats_update(&stats, 3, 0);
    }

    syn_control_stats_report(&stats, &report);
    TEST_ASSERT_EQUAL_INT32(3, report.rms_error);
}

static void test_control_stats_negative_error(void)
{
    SYN_ControlStats stats;
    SYN_ControlReport report;

    syn_control_stats_reset(&stats);

    /* Alternating ±5 error → RMS should still be 5 */
    syn_control_stats_update(&stats, 5, 0);
    syn_control_stats_update(&stats, -5, 0);
    syn_control_stats_update(&stats, 5, 0);
    syn_control_stats_update(&stats, -5, 0);

    syn_control_stats_report(&stats, &report);
    TEST_ASSERT_EQUAL_INT32(5, report.rms_error);
    TEST_ASSERT_EQUAL_INT32(5, report.max_error);
}

static void test_control_stats_itae_grows_with_time(void)
{
    SYN_ControlStats stats;
    SYN_ControlReport report;

    syn_control_stats_reset(&stats);

    /* Constant error of 10 for 5 samples */
    /* ITAE = sum(i * |error|) for i = 0..4 = 10*(0+1+2+3+4) = 100 */
    /* Average ITAE = 100 / 5 = 20 */
    for (int i = 0; i < 5; i++) {
        syn_control_stats_update(&stats, 10, 0);
    }

    syn_control_stats_report(&stats, &report);
    TEST_ASSERT_EQUAL_INT32(20, report.itae);
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

void run_control_stats_tests(void)
{
    RUN_TEST(test_control_stats_reset);
    RUN_TEST(test_control_stats_report_zero_samples);
    RUN_TEST(test_control_stats_single_sample);
    RUN_TEST(test_control_stats_max_error_tracking);
    RUN_TEST(test_control_stats_jitter);
    RUN_TEST(test_control_stats_rms_error);
    RUN_TEST(test_control_stats_negative_error);
    RUN_TEST(test_control_stats_itae_grows_with_time);
}
