/**
 * @file test_trace.c
 * @brief Unity tests for syn_trace — full coverage.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/debug/syn_trace.h"

#include <string.h>

/* ── Print capture ─────────────────────────────────────────────────────── */

static char trace_out[2048];
static size_t trace_out_pos = 0;

static void trace_print(const char *str)
{
    while (str && *str && trace_out_pos < sizeof(trace_out) - 1) {
        trace_out[trace_out_pos++] = *str++;
    }
    trace_out[trace_out_pos] = '\0';
}

static void trace_out_clear(void)
{
    trace_out_pos = 0;
    trace_out[0] = '\0';
}

/* ── Test: basic recording and read-back ──────────────────────────────── */

static void test_trace(void)
{
    mock_tick_ms = 0;

    SYN_TraceEntry entries[4];
    SYN_Trace trace;
    syn_trace_init(&trace, entries, 4);

    TEST_ASSERT_EQUAL_INT(0, syn_trace_count(&trace));

    /* Record some events */
    mock_tick_ms = 100;
    syn_trace_record(&trace, 0x01, 42);
    mock_tick_ms = 200;
    syn_trace_record(&trace, 0x02, 99);
    TEST_ASSERT_EQUAL_INT(2, syn_trace_count(&trace));

    /* Read back */
    SYN_TraceEntry e;
    TEST_ASSERT_TRUE(syn_trace_read(&trace, 0, &e));
    TEST_ASSERT_EQUAL_HEX8(0x01, e.event_id);
    TEST_ASSERT_EQUAL_INT(42, e.value);
    TEST_ASSERT_EQUAL_INT(100, e.timestamp);

    TEST_ASSERT_TRUE(syn_trace_read(&trace, 1, &e));
    TEST_ASSERT_EQUAL_HEX8(0x02, e.event_id);

    /* Overflow wraps (capacity=4, add 4 more → wraps) */
    mock_tick_ms = 300;
    syn_trace_record(&trace, 0x03, 0);
    syn_trace_record(&trace, 0x04, 0);
    syn_trace_record(&trace, 0x05, 0);  /* wraps, overwrites slot 0 */
    syn_trace_record(&trace, 0x06, 0);

    TEST_ASSERT_EQUAL_INT(6, syn_trace_count(&trace));
    /* Oldest available should now be event 0x03 */
    TEST_ASSERT_TRUE(syn_trace_read(&trace, 0, &e));
    TEST_ASSERT_EQUAL_HEX8(0x03, e.event_id);

    /* Disable */
    syn_trace_enable(&trace, false);
    syn_trace_record(&trace, 0xFF, 0);
    TEST_ASSERT_EQUAL_INT(6, syn_trace_count(&trace));

    /* Clear */
    syn_trace_enable(&trace, true);
    syn_trace_clear(&trace);
    TEST_ASSERT_EQUAL_INT(0, syn_trace_count(&trace));
}

/* ── Test: read out-of-range returns false ────────────────────────────── */

static void test_trace_read_oob(void)
{
    SYN_TraceEntry entries[4];
    SYN_Trace trace;
    syn_trace_init(&trace, entries, 4);

    SYN_TraceEntry e;
    /* Empty trace — any index is out of bounds */
    TEST_ASSERT_FALSE(syn_trace_read(&trace, 0, &e));
    TEST_ASSERT_FALSE(syn_trace_read(&trace, 99, &e));

    /* After adding 2 entries, index 2 is out of bounds */
    syn_trace_record(&trace, 0x01, 1);
    syn_trace_record(&trace, 0x02, 2);
    TEST_ASSERT_FALSE(syn_trace_read(&trace, 2, &e));
}

/* ── Test: dump function — normal entries ─────────────────────────────── */

static void test_trace_dump(void)
{
    mock_tick_ms = 0;

    SYN_TraceEntry entries[4];
    SYN_Trace trace;
    syn_trace_init(&trace, entries, 4);

    /* Add entries at various timestamps */
    mock_tick_ms = 1;
    syn_trace_record(&trace, 0xABCD, 0x1234);
    mock_tick_ms = 9999999; /* large value — exercises padding */
    syn_trace_record(&trace, 0x0001, 0x0002);

    trace_out_clear();
    syn_trace_dump(&trace, trace_print);

    TEST_ASSERT_NOT_NULL(strstr(trace_out, "evt=0x"));
    TEST_ASSERT_NOT_NULL(strstr(trace_out, "val=0x"));
    TEST_ASSERT_NOT_NULL(strstr(trace_out, "ABCD")); /* hex upper-case */
}

/* ── Test: dump with NULL print — early return ────────────────────────── */

static void test_trace_dump_null_print(void)
{
    SYN_TraceEntry entries[2];
    SYN_Trace trace;
    syn_trace_init(&trace, entries, 2);
    syn_trace_record(&trace, 0x01, 0);

    /* Should not crash */
    syn_trace_dump(&trace, NULL);
}

/* ── Test: dump when empty (available==0) ─────────────────────────────── */

static void test_trace_dump_empty(void)
{
    SYN_TraceEntry entries[4];
    SYN_Trace trace;
    syn_trace_init(&trace, entries, 4);

    trace_out_clear();
    syn_trace_dump(&trace, trace_print);

    /* No output for empty trace */
    TEST_ASSERT_EQUAL_INT(0, (int)trace_out_pos);
}

/* ── Test: dump after overflow (wrapped ring buffer path) ─────────────── */

static void test_trace_dump_wrapped(void)
{
    mock_tick_ms = 500;

    SYN_TraceEntry entries[3];
    SYN_Trace trace;
    syn_trace_init(&trace, entries, 3);

    /* Fill and overflow — count > capacity */
    syn_trace_record(&trace, 0x01, 1);
    syn_trace_record(&trace, 0x02, 2);
    syn_trace_record(&trace, 0x03, 3);
    syn_trace_record(&trace, 0x04, 4); /* overwrites slot 0 */

    trace_out_clear();
    syn_trace_dump(&trace, trace_print);

    /* Should output exactly 3 entries (capacity) */
    TEST_ASSERT_NOT_NULL(strstr(trace_out, "evt=0x"));
}

/* ── Test runner ─────────────────────────────────────────────────────── */

void run_trace_tests(void)
{
    RUN_TEST(test_trace);
    RUN_TEST(test_trace_read_oob);
    RUN_TEST(test_trace_dump);
    RUN_TEST(test_trace_dump_null_print);
    RUN_TEST(test_trace_dump_empty);
    RUN_TEST(test_trace_dump_wrapped);
}
