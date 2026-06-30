/**
 * @file test_watchdog.c
 * @brief Unity tests for syn_watchdog.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/sched/syn_watchdog.h"

static int wdt_timeout_count = 0;
static const char *wdt_timeout_name = NULL;

static void wdt_on_timeout(SYN_Watchdog *wdt, const SYN_WDT_Entry *entry,
                           void *ctx)
{
    (void)wdt; (void)ctx;
    wdt_timeout_count++;
    wdt_timeout_name = entry->name;
}

static void test_watchdog(void)
{

    mock_tick_ms = 0;
    wdt_timeout_count = 0;

    SYN_Watchdog wdt;
    SYN_WDT_Entry entries[4];
    syn_watchdog_init(&wdt, entries, 4, wdt_on_timeout, NULL);

    int8_t id0 = syn_watchdog_register(&wdt, "task_a", 100);
    int8_t id1 = syn_watchdog_register(&wdt, "task_b", 200);
    TEST_ASSERT_TRUE(id0 >= 0);
    TEST_ASSERT_TRUE(id1 >= 0);

    /* No timeout yet */
    mock_tick_advance(50);
    syn_watchdog_update(&wdt);
    TEST_ASSERT_EQUAL_INT(0, wdt_timeout_count);

    /* Check in task_a */
    syn_watchdog_checkin(&wdt, id0);

    /* task_a deadline at 150ms, task_b at 200ms */
    mock_tick_advance(60);  /* now 110ms */
    syn_watchdog_update(&wdt);
    TEST_ASSERT_EQUAL_INT(0, wdt_timeout_count);

    mock_tick_advance(100); /* now 210ms — task_b should time out */
    syn_watchdog_update(&wdt);
    TEST_ASSERT_TRUE(wdt_timeout_count >= 1);
    TEST_ASSERT_EQUAL_INT(0, strcmp(wdt_timeout_name, "task_b"));

    /* Unregister */
    syn_watchdog_unregister(&wdt, id1);
    wdt_timeout_count = 0;
    mock_tick_advance(500);
    syn_watchdog_update(&wdt);
    /* Only id0 should fire (id1 unregistered) */
    TEST_ASSERT_TRUE(wdt_timeout_count >= 1);
}

/** table full → register returns -1 (line 55); errlog on timeout (lines 88-89) */
static void test_watchdog_table_full_and_errlog(void)
{
    mock_tick_ms = 0;
    wdt_timeout_count = 0;

    SYN_Watchdog wdt;
    SYN_WDT_Entry entries[2]; /* tiny 2-slot table */
    syn_watchdog_init(&wdt, entries, 2, wdt_on_timeout, NULL);

    /* Attach errlog */
    static SYN_ErrEntry errlog_buf[8];
    static SYN_ErrLog errlog;
    syn_errlog_init(&errlog, errlog_buf, 8, 1);
    wdt.errlog = &errlog;

    int8_t id0 = syn_watchdog_register(&wdt, "a", 100);
    int8_t id1 = syn_watchdog_register(&wdt, "b", 100);
    TEST_ASSERT_TRUE(id0 >= 0);
    TEST_ASSERT_TRUE(id1 >= 0);

    /* Table full — returns -1 (line 55) */
    int8_t id2 = syn_watchdog_register(&wdt, "c", 100);
    TEST_ASSERT_EQUAL_INT(-1, id2);

    /* Trigger timeout — errlog records it (lines 88-89) */
    mock_tick_advance(200);
    syn_watchdog_update(&wdt);
    TEST_ASSERT_TRUE(wdt_timeout_count >= 1);
    TEST_ASSERT_TRUE(syn_errlog_count(&errlog) > 0);
}

void run_watchdog_tests(void)
{
    RUN_TEST(test_watchdog);
    RUN_TEST(test_watchdog_table_full_and_errlog);
}
