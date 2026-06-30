/**
 * @file test_i2c_async.c
 * @brief Tests for async I2C port abstraction (mock-based).
 */

#include "unity/unity.h"
#include "syntropic/port/syn_port_i2c_async.h"
#include "mocks/mock_port.h"

#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

static int cb_count;
static uint8_t cb_bus;
static SYN_Status cb_result;
static void *cb_ctx;

static void i2c_done(uint8_t bus, SYN_Status result, void *ctx)
{
    cb_count++;
    cb_bus = bus;
    cb_result = result;
    cb_ctx = ctx;
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

void test_i2c_async_write_only(void)
{
    cb_count = 0;
    uint8_t tx[] = {0x6B, 0x00};
    SYN_I2C_Xfer xfer = {
        .bus = 0, .addr = 0x68,
        .tx_data = tx, .tx_len = 2,
        .rx_data = NULL, .rx_len = 0,
        .callback = i2c_done,
        .user_data = (void *)0xA1,
    };

    TEST_ASSERT_EQUAL(SYN_OK, syn_port_i2c_xfer_async(&xfer));
    TEST_ASSERT_EQUAL(1, mock_i2c_async_count);
    TEST_ASSERT_TRUE(mock_i2c_async_busy);

    mock_i2c_async_complete();
    TEST_ASSERT_EQUAL(1, cb_count);
    TEST_ASSERT_EQUAL(0, cb_bus);
    TEST_ASSERT_EQUAL(SYN_OK, cb_result);
    TEST_ASSERT_EQUAL_PTR((void *)0xA1, cb_ctx);
    TEST_ASSERT_FALSE(mock_i2c_async_busy);
}

void test_i2c_async_read_only(void)
{
    cb_count = 0;
    uint8_t rx[4] = {0};
    SYN_I2C_Xfer xfer = {
        .bus = 1, .addr = 0x76,
        .tx_data = NULL, .tx_len = 0,
        .rx_data = rx, .rx_len = 4,
        .callback = i2c_done,
    };

    TEST_ASSERT_EQUAL(SYN_OK, syn_port_i2c_xfer_async(&xfer));
    mock_i2c_async_complete();
    TEST_ASSERT_EQUAL(1, cb_count);
    TEST_ASSERT_EQUAL(1, cb_bus);
}

void test_i2c_async_write_read(void)
{
    cb_count = 0;
    uint8_t reg = 0xD0;
    uint8_t val;
    SYN_I2C_Xfer xfer = {
        .bus = 0, .addr = 0x76,
        .tx_data = &reg, .tx_len = 1,
        .rx_data = &val, .rx_len = 1,
        .callback = i2c_done,
    };

    TEST_ASSERT_EQUAL(SYN_OK, syn_port_i2c_xfer_async(&xfer));
    mock_i2c_async_complete();
    TEST_ASSERT_EQUAL(1, cb_count);
}

void test_i2c_async_busy_rejects(void)
{
    uint8_t tx = 0;
    SYN_I2C_Xfer xfer = {
        .bus = 0, .addr = 0x50,
        .tx_data = &tx, .tx_len = 1,
        .callback = i2c_done,
    };

    syn_port_i2c_xfer_async(&xfer);
    TEST_ASSERT_EQUAL(SYN_BUSY, syn_port_i2c_xfer_async(&xfer));
    mock_i2c_async_complete();
}

void test_i2c_async_cancel(void)
{
    cb_count = 0;
    uint8_t tx = 0;
    SYN_I2C_Xfer xfer = {
        .bus = 0, .addr = 0x50,
        .tx_data = &tx, .tx_len = 1,
        .callback = i2c_done,
    };

    syn_port_i2c_xfer_async(&xfer);
    TEST_ASSERT_EQUAL(SYN_OK, syn_port_i2c_cancel(0));
    TEST_ASSERT_FALSE(mock_i2c_async_busy);

    /* Completing after cancel should be a no-op */
    mock_i2c_async_complete();
    TEST_ASSERT_EQUAL(0, cb_count);
}

void test_i2c_async_cancel_when_idle(void)
{
    TEST_ASSERT_EQUAL(SYN_ERROR, syn_port_i2c_cancel(0));
}

void test_i2c_async_error_result(void)
{
    cb_count = 0;
    uint8_t tx = 0;
    SYN_I2C_Xfer xfer = {
        .bus = 0, .addr = 0x50,
        .tx_data = &tx, .tx_len = 1,
        .callback = i2c_done,
    };

    mock_i2c_async_result = SYN_ERROR;
    syn_port_i2c_xfer_async(&xfer);
    mock_i2c_async_complete();
    TEST_ASSERT_EQUAL(SYN_ERROR, cb_result);
}

void test_i2c_async_null_xfer(void)
{
    TEST_ASSERT_EQUAL(SYN_ERROR, syn_port_i2c_xfer_async(NULL));
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

void run_i2c_async_tests(void)
{
    RUN_TEST(test_i2c_async_write_only);
    RUN_TEST(test_i2c_async_read_only);
    RUN_TEST(test_i2c_async_write_read);
    RUN_TEST(test_i2c_async_busy_rejects);
    RUN_TEST(test_i2c_async_cancel);
    RUN_TEST(test_i2c_async_cancel_when_idle);
    RUN_TEST(test_i2c_async_error_result);
    RUN_TEST(test_i2c_async_null_xfer);
}
