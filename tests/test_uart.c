#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/drivers/syn_uart.h"
#include <string.h>

static SYN_UART uart;

/** Init: success path */
static void test_uart_init(void)
{
    SYN_Status st = syn_uart_init(&uart, 0, 115200);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_TRUE(uart.initialized);
    TEST_ASSERT_EQUAL(0, uart.instance);
}

/** Deinit: success path */
static void test_uart_deinit(void)
{
    syn_uart_init(&uart, 0, 115200);
    SYN_Status st = syn_uart_deinit(&uart);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_FALSE(uart.initialized);
}

/** Deinit when not initialized — returns OK without calling port */
static void test_uart_deinit_not_initialized(void)
{
    memset(&uart, 0, sizeof(uart));
    uart.initialized = false;
    SYN_Status st = syn_uart_deinit(&uart);
    TEST_ASSERT_EQUAL(SYN_OK, st);
}

/** Write string */
static void test_uart_write_str(void)
{
    syn_uart_init(&uart, 0, 115200);
    mock_uart_tx_len = 0;
    SYN_Status st = syn_uart_write_str(&uart, "hello", 1000);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_EQUAL(5, mock_uart_tx_len);
    TEST_ASSERT_EQUAL_UINT8('h', mock_uart_tx_buf[0]);
}

/** Write string: empty string */
static void test_uart_write_str_empty(void)
{
    syn_uart_init(&uart, 0, 115200);
    mock_uart_tx_len = 0;
    SYN_Status st = syn_uart_write_str(&uart, "", 1000);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_EQUAL(0, mock_uart_tx_len);
}

/** Write bytes */
static void test_uart_write(void)
{
    syn_uart_init(&uart, 0, 115200);
    mock_uart_tx_len = 0;
    uint8_t data[] = { 0xAA, 0xBB, 0xCC };
    SYN_Status st = syn_uart_write(&uart, data, sizeof(data), 1000);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_EQUAL(3, mock_uart_tx_len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, mock_uart_tx_buf[0]);
}

/** Write bytes: zero length */
static void test_uart_write_zero_len(void)
{
    syn_uart_init(&uart, 0, 115200);
    mock_uart_tx_len = 0;
    SYN_Status st = syn_uart_write(&uart, NULL, 0, 1000);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_EQUAL(0, mock_uart_tx_len);
}

/** Read from rx ringbuffer */
static void test_uart_read(void)
{
    syn_uart_init(&uart, 0, 115200);
    /* Feed bytes via ISR */
    syn_uart_rx_isr_feed(&uart, 'A');
    syn_uart_rx_isr_feed(&uart, 'B');
    syn_uart_rx_isr_feed(&uart, 'C');

    uint8_t buf[8];
    size_t n = syn_uart_read(&uart, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('C', buf[2]);
}

/** Read when empty */
static void test_uart_read_empty(void)
{
    syn_uart_init(&uart, 0, 115200);
    uint8_t buf[8];
    size_t n = syn_uart_read(&uart, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, n);
}

/** rx_isr_feed: feed and verify */
static void test_uart_rx_isr_feed(void)
{
    syn_uart_init(&uart, 0, 115200);
    bool ok = syn_uart_rx_isr_feed(&uart, 0x42);
    TEST_ASSERT_TRUE(ok);
}

/** Init: port fails */
static void test_uart_init_fail(void)
{
    mock_uart_init_fail = true;
    SYN_Status st = syn_uart_init(&uart, 0, 115200);
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
    TEST_ASSERT_FALSE(uart.initialized);
    mock_uart_init_fail = false;
}

void run_uart_tests(void)
{
    RUN_TEST(test_uart_init);
    RUN_TEST(test_uart_init_fail);
    RUN_TEST(test_uart_deinit);
    RUN_TEST(test_uart_deinit_not_initialized);
    RUN_TEST(test_uart_write_str);
    RUN_TEST(test_uart_write_str_empty);
    RUN_TEST(test_uart_write);
    RUN_TEST(test_uart_write_zero_len);
    RUN_TEST(test_uart_read);
    RUN_TEST(test_uart_read_empty);
    RUN_TEST(test_uart_rx_isr_feed);
}
