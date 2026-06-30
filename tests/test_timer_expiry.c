/**
 * @file test_timer_expiry.c
 * @brief Tests for syn_timer_next_expiry() and tickless+timer integration.
 */

#include "unity/unity.h"
#include "syntropic/sched/syn_timer.h"
#include "mocks/mock_port.h"

#include <string.h>
#include <limits.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

static void dummy_cb(SYN_Timer *t, void *ctx)
{
    (void)t; (void)ctx;
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

void test_timer_next_expiry_no_active(void)
{
    SYN_Timer timers[3];
    for (int i = 0; i < 3; i++) {
        syn_timer_init(&timers[i], 100, false, dummy_cb, NULL);
        /* Not started — active == false */
    }

    uint32_t exp = syn_timer_next_expiry(timers, 3);
    TEST_ASSERT_EQUAL(UINT32_MAX, exp);
}

void test_timer_next_expiry_one_active(void)
{
    mock_tick_ms = 1000;

    SYN_Timer timers[3];
    syn_timer_init(&timers[0], 200, false, dummy_cb, NULL);
    syn_timer_init(&timers[1], 500, false, dummy_cb, NULL);
    syn_timer_init(&timers[2], 300, false, dummy_cb, NULL);

    /* Only start timer[1] → target_tick = 1000 + 500 = 1500 */
    syn_timer_start(&timers[1]);

    uint32_t exp = syn_timer_next_expiry(timers, 3);
    TEST_ASSERT_EQUAL(1500, exp);
}

void test_timer_next_expiry_multiple_returns_earliest(void)
{
    mock_tick_ms = 0;

    SYN_Timer timers[3];
    syn_timer_init(&timers[0], 300, false, dummy_cb, NULL);
    syn_timer_init(&timers[1], 100, false, dummy_cb, NULL);
    syn_timer_init(&timers[2], 200, false, dummy_cb, NULL);

    syn_timer_start(&timers[0]); /* target = 300 */
    syn_timer_start(&timers[1]); /* target = 100 ← earliest */
    syn_timer_start(&timers[2]); /* target = 200 */

    uint32_t exp = syn_timer_next_expiry(timers, 3);
    TEST_ASSERT_EQUAL(100, exp);
}

void test_timer_next_expiry_expired_returns_now(void)
{
    mock_tick_ms = 0;
    SYN_Timer t;
    syn_timer_init(&t, 50, false, dummy_cb, NULL);
    syn_timer_start(&t); /* target = 50 */

    /* Advance past expiry */
    mock_tick_ms = 100;

    uint32_t exp = syn_timer_next_expiry(&t, 1);
    /* Expired timer → returns 'now' */
    TEST_ASSERT_EQUAL(100, exp);
}

void test_timer_next_expiry_empty_array(void)
{
    uint32_t exp = syn_timer_next_expiry(NULL, 0);
    TEST_ASSERT_EQUAL(UINT32_MAX, exp);
}

void test_timer_next_expiry_mix_expired_and_future(void)
{
    mock_tick_ms = 0;
    SYN_Timer timers[2];
    syn_timer_init(&timers[0], 50, false, dummy_cb, NULL);
    syn_timer_init(&timers[1], 200, false, dummy_cb, NULL);
    syn_timer_start(&timers[0]); /* target = 50 */
    syn_timer_start(&timers[1]); /* target = 200 */

    /* Advance past timer[0] but not timer[1] */
    mock_tick_ms = 100;

    uint32_t exp = syn_timer_next_expiry(timers, 2);
    /* timer[0] is expired → returns 'now' (100) */
    TEST_ASSERT_EQUAL(100, exp);
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

void run_timer_expiry_tests(void)
{
    RUN_TEST(test_timer_next_expiry_no_active);
    RUN_TEST(test_timer_next_expiry_one_active);
    RUN_TEST(test_timer_next_expiry_multiple_returns_earliest);
    RUN_TEST(test_timer_next_expiry_expired_returns_now);
    RUN_TEST(test_timer_next_expiry_empty_array);
    RUN_TEST(test_timer_next_expiry_mix_expired_and_future);
}
