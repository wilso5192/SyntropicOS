/**
 * @file test_log.c
 * @brief Unity tests for syn_log.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/log/syn_log.h"

static char log_capture_buf[1024];
static size_t log_capture_pos = 0;

static void log_capture_output(const char *str, size_t len)
{
    size_t space = sizeof(log_capture_buf) - log_capture_pos - 1;
    if (len > space) len = space;
    memcpy(log_capture_buf + log_capture_pos, str, len);
    log_capture_pos += len;
    log_capture_buf[log_capture_pos] = '\0';
}

static void test_logging_basic(void)
{
    mock_tick_ms = 1234;
    log_capture_pos = 0;
    log_capture_buf[0] = '\0';

    syn_log_init(log_capture_output, SYN_LOG_DEBUG);

    syn_log(SYN_LOG_DEBUG, "test", "hello %d", 42);
    TEST_ASSERT_TRUE(log_capture_pos > 0);
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "D/test:"));
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "hello 42"));
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "1234"));

    log_capture_pos = 0;
    syn_log(SYN_LOG_INFO, "net", "connected");
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "I/net:"));

    log_capture_pos = 0;
    SYN_LOG_T("test", "%s", "this should not appear");
    TEST_ASSERT_TRUE(log_capture_pos == 0);

    syn_log_set_level(SYN_LOG_ERROR);
    log_capture_pos = 0;
    syn_log(SYN_LOG_WARN, "test", "warn msg");
    TEST_ASSERT_TRUE(log_capture_pos == 0);

    log_capture_pos = 0;
    syn_log(SYN_LOG_ERROR, "test", "error msg");
    TEST_ASSERT_TRUE(log_capture_pos > 0);

    TEST_ASSERT_TRUE(syn_log_get_level() == SYN_LOG_ERROR);

    syn_log_set_level(SYN_LOG_DEBUG);
    log_capture_pos = 0;
    syn_log_raw("raw text\n");
    TEST_ASSERT_EQUAL_STRING("raw text\n", log_capture_buf);

    log_capture_pos = 0;
    syn_log_raw(NULL);
    TEST_ASSERT_TRUE(log_capture_pos == 0);

    syn_log_init(NULL, SYN_LOG_DEBUG);
    syn_log(SYN_LOG_INFO, "test", "no crash");
    TEST_ASSERT_TRUE(1);
}

static void test_log_hexdump(void)
{
    log_capture_pos = 0;
    log_capture_buf[0] = '\0';

    syn_log_init(log_capture_output, SYN_LOG_DEBUG);

    uint8_t data[20];
    for (int i = 0; i < 20; i++) {
        data[i] = (i < 10) ? ('0' + i) : (i - 10);
    }

    syn_log_hexdump("dump", data, 20);

    TEST_ASSERT_TRUE(log_capture_pos > 0);
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "0000"));
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "0010"));
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "0123456789"));
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "......"));

    /* Test with NULL data/output */
    log_capture_pos = 0;
    syn_log_hexdump("dump", NULL, 20);
    TEST_ASSERT_TRUE(log_capture_pos == 0);

    syn_log_init(NULL, SYN_LOG_DEBUG);
    syn_log_hexdump("dump", data, 20);
    TEST_ASSERT_TRUE(1); /* No crash */
}

static void test_log_invalid_level(void)
{
    log_capture_pos = 0;
    log_capture_buf[0] = '\0';
    syn_log_init(log_capture_output, SYN_LOG_TRACE);

    syn_log((SYN_LogLevel)10, "test", "invalid level message");
    TEST_ASSERT_TRUE(log_capture_pos > 0);
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "?/test:"));
}

static void test_log_null_tag(void)
{
    log_capture_pos = 0;
    log_capture_buf[0] = '\0';
    syn_log_init(log_capture_output, SYN_LOG_DEBUG);

    syn_log(SYN_LOG_DEBUG, NULL, "no tag message");
    TEST_ASSERT_TRUE(log_capture_pos > 0);
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "D: no tag message"));

    log_capture_pos = 0;
    log_capture_buf[0] = '\0';
    syn_log(SYN_LOG_DEBUG, "", "empty tag message");
    TEST_ASSERT_TRUE(log_capture_pos > 0);
    TEST_ASSERT_NOT_NULL(strstr(log_capture_buf, "D: empty tag message"));
}

void run_log_tests(void)
{
    RUN_TEST(test_logging_basic);
    RUN_TEST(test_log_hexdump);
    RUN_TEST(test_log_invalid_level);
    RUN_TEST(test_log_null_tag);
}

