/**
 * @file test_spi_async.c
 * @brief Tests for async SPI port abstraction (mock-based).
 */

#include "unity/unity.h"
#include "syntropic/port/syn_port_spi_async.h"
#include "mocks/mock_port.h"

#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

static int cb_count;
static uint8_t cb_bus;
static SYN_Status cb_result;
static void *cb_ctx;

static void spi_done(uint8_t bus, SYN_Status result, void *ctx)
{
    cb_count++;
    cb_bus = bus;
    cb_result = result;
    cb_ctx = ctx;
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

void test_spi_async_full_duplex(void)
{
    cb_count = 0;
    uint8_t tx[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rx[4] = {0};
    SYN_SPI_Xfer xfer = {
        .bus = 0,
        .tx_buf = tx, .rx_buf = rx, .len = 4,
        .callback = spi_done,
        .user_data = (void *)0xB2,
    };

    TEST_ASSERT_EQUAL(SYN_OK, syn_port_spi_xfer_async(&xfer));
    TEST_ASSERT_EQUAL(1, mock_spi_async_count);
    TEST_ASSERT_TRUE(mock_spi_async_busy);

    mock_spi_async_complete();
    TEST_ASSERT_EQUAL(1, cb_count);
    TEST_ASSERT_EQUAL(0, cb_bus);
    TEST_ASSERT_EQUAL(SYN_OK, cb_result);
    TEST_ASSERT_EQUAL_PTR((void *)0xB2, cb_ctx);
    TEST_ASSERT_FALSE(mock_spi_async_busy);
}

void test_spi_async_tx_only(void)
{
    cb_count = 0;
    uint8_t tx[8] = {0};
    SYN_SPI_Xfer xfer = {
        .bus = 0,
        .tx_buf = tx, .rx_buf = NULL, .len = 8,
        .callback = spi_done,
    };

    TEST_ASSERT_EQUAL(SYN_OK, syn_port_spi_xfer_async(&xfer));
    mock_spi_async_complete();
    TEST_ASSERT_EQUAL(1, cb_count);
}

void test_spi_async_rx_only(void)
{
    cb_count = 0;
    uint8_t rx[8] = {0};
    SYN_SPI_Xfer xfer = {
        .bus = 1,
        .tx_buf = NULL, .rx_buf = rx, .len = 8,
        .callback = spi_done,
    };

    TEST_ASSERT_EQUAL(SYN_OK, syn_port_spi_xfer_async(&xfer));
    mock_spi_async_complete();
    TEST_ASSERT_EQUAL(1, cb_count);
    TEST_ASSERT_EQUAL(1, cb_bus);
}

void test_spi_async_busy_rejects(void)
{
    uint8_t tx = 0;
    SYN_SPI_Xfer xfer = {
        .bus = 0,
        .tx_buf = &tx, .len = 1,
        .callback = spi_done,
    };

    syn_port_spi_xfer_async(&xfer);
    TEST_ASSERT_EQUAL(SYN_BUSY, syn_port_spi_xfer_async(&xfer));
    mock_spi_async_complete();
}

void test_spi_async_cancel(void)
{
    cb_count = 0;
    uint8_t tx = 0;
    SYN_SPI_Xfer xfer = {
        .bus = 0,
        .tx_buf = &tx, .len = 1,
        .callback = spi_done,
    };

    syn_port_spi_xfer_async(&xfer);
    TEST_ASSERT_EQUAL(SYN_OK, syn_port_spi_cancel(0));
    TEST_ASSERT_FALSE(mock_spi_async_busy);

    mock_spi_async_complete();
    TEST_ASSERT_EQUAL(0, cb_count);
}

void test_spi_async_cancel_when_idle(void)
{
    TEST_ASSERT_EQUAL(SYN_ERROR, syn_port_spi_cancel(0));
}

void test_spi_async_error_result(void)
{
    cb_count = 0;
    uint8_t tx = 0;
    SYN_SPI_Xfer xfer = {
        .bus = 0,
        .tx_buf = &tx, .len = 1,
        .callback = spi_done,
    };

    mock_spi_async_result = SYN_ERROR;
    syn_port_spi_xfer_async(&xfer);
    mock_spi_async_complete();
    TEST_ASSERT_EQUAL(SYN_ERROR, cb_result);
}

void test_spi_async_null_xfer(void)
{
    TEST_ASSERT_EQUAL(SYN_ERROR, syn_port_spi_xfer_async(NULL));
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

void run_spi_async_tests(void)
{
    RUN_TEST(test_spi_async_full_duplex);
    RUN_TEST(test_spi_async_tx_only);
    RUN_TEST(test_spi_async_rx_only);
    RUN_TEST(test_spi_async_busy_rejects);
    RUN_TEST(test_spi_async_cancel);
    RUN_TEST(test_spi_async_cancel_when_idle);
    RUN_TEST(test_spi_async_error_result);
    RUN_TEST(test_spi_async_null_xfer);
}
