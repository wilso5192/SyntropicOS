/**
 * @file test_timer.c
 * @brief Unity tests for syn_timer.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/sched/syn_timer.h"

static int timer_fire_count = 0;

static void timer_callback(SYN_Timer *t, void *ctx)
{
    (void)t; (void)ctx;
    timer_fire_count++;
}

static void test_software_timer(void)
{
    SYN_Timer tmr;
    mock_tick_ms = 0;
    timer_fire_count = 0;

    syn_timer_init(&tmr, 100, true, timer_callback, NULL);
    syn_timer_start(&tmr);

    /* Service at t=50 — should not fire */
    mock_tick_advance(50);
    syn_timer_service(&tmr, 1);
    TEST_ASSERT_EQUAL_INT(0, timer_fire_count);

    /* Service at t=100 — should fire */
    mock_tick_advance(50);
    syn_timer_service(&tmr, 1);
    TEST_ASSERT_EQUAL_INT(1, timer_fire_count);

    /* Service at t=200 — should fire again (periodic) */
    mock_tick_advance(100);
    syn_timer_service(&tmr, 1);
    TEST_ASSERT_EQUAL_INT(2, timer_fire_count);

    /* Stop and verify no more fires */
    syn_timer_stop(&tmr);
    mock_tick_advance(200);
    syn_timer_service(&tmr, 1);
    TEST_ASSERT_EQUAL_INT(2, timer_fire_count);
}

static void test_timeout(void)
{
    mock_tick_ms = 0;
    SYN_Timeout to;

    syn_timeout_start(&to, 100);
    TEST_ASSERT_FALSE(syn_timeout_expired(&to));
    TEST_ASSERT_EQUAL_INT(100, syn_timeout_remaining(&to));

    mock_tick_advance(50);
    TEST_ASSERT_FALSE(syn_timeout_expired(&to));
    TEST_ASSERT_EQUAL_INT(50, syn_timeout_elapsed(&to));
    TEST_ASSERT_EQUAL_INT(50, syn_timeout_remaining(&to));

    mock_tick_advance(50);
    TEST_ASSERT_TRUE(syn_timeout_expired(&to));
    TEST_ASSERT_EQUAL_INT(0, syn_timeout_remaining(&to));

    /* Restart */
    syn_timeout_restart(&to);
    TEST_ASSERT_FALSE(syn_timeout_expired(&to));

    /* Periodic */
    mock_tick_ms = 0;
    syn_timeout_start(&to, 50);
    TEST_ASSERT_FALSE(syn_timeout_periodic(&to));

    mock_tick_advance(50);
    TEST_ASSERT_TRUE(syn_timeout_periodic(&to));
    TEST_ASSERT_FALSE(syn_timeout_periodic(&to));

    mock_tick_advance(50);
    TEST_ASSERT_TRUE(syn_timeout_periodic(&to));
}

static void test_timer_extensions(void)
{
    SYN_Timer tmr;
    mock_tick_ms = 0;
    timer_fire_count = 0;

    /* 1. Init inactive timer and check syn_timer_expired */
    syn_timer_init(&tmr, 100, false, timer_callback, NULL);
    TEST_ASSERT_FALSE(syn_timer_expired(&tmr));

    /* 2. Set period & remaining */
    TEST_ASSERT_EQUAL_INT(0, syn_timer_remaining(&tmr));
    syn_timer_set_period(&tmr, 200);
    TEST_ASSERT_EQUAL_INT(200, tmr.period_ms);
    syn_timer_start(&tmr);
    TEST_ASSERT_EQUAL_INT(200, syn_timer_remaining(&tmr));

    mock_tick_advance(50);
    TEST_ASSERT_EQUAL_INT(150, syn_timer_remaining(&tmr));

    /* 3. Expiration of one-shot timer */
    mock_tick_advance(150); /* t=200 */
    TEST_ASSERT_TRUE(syn_timer_expired(&tmr));
    TEST_ASSERT_FALSE(tmr.active);
    TEST_ASSERT_EQUAL_INT(0, syn_timer_remaining(&tmr));

    /* 4. Timer service with NULL callback */
    SYN_Timer null_cb_tmr;
    syn_timer_init(&null_cb_tmr, 100, false, NULL, NULL);
    syn_timer_start(&null_cb_tmr);
    mock_tick_advance(100);
    syn_timer_service(&null_cb_tmr, 1);
    TEST_ASSERT_FALSE(null_cb_tmr.active);

    /* 5. Periodic drift and snap forward */
    SYN_Timer periodic_tmr;
    syn_timer_init(&periodic_tmr, 100, true, timer_callback, NULL);
    syn_timer_start(&periodic_tmr); /* target = 100 */

    mock_tick_advance(250); /* t = 350 */
    TEST_ASSERT_TRUE(syn_timer_expired(&periodic_tmr));
    TEST_ASSERT_EQUAL_INT(mock_tick_ms + periodic_tmr.period_ms, periodic_tmr.target_tick); /* snapped forward to now + period */

    syn_timer_stop(&periodic_tmr);
}

void run_timer_tests(void)
{
    RUN_TEST(test_software_timer);
    RUN_TEST(test_timeout);
    RUN_TEST(test_timer_extensions);
}

