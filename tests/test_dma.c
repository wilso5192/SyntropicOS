/**
 * @file test_dma.c
 * @brief Tests for DMA port abstraction (mock-based).
 */

#include "unity/unity.h"
#include "syntropic/port/syn_port_dma.h"
#include "mocks/mock_port.h"

#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

static int callback_count;
static uint8_t callback_channel;
static SYN_Status callback_result;
static void *callback_ctx;

static void dma_cb(uint8_t channel, SYN_Status result, void *ctx)
{
    callback_count++;
    callback_channel = channel;
    callback_result = result;
    callback_ctx = ctx;
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

void test_dma_init_and_start(void)
{
    SYN_DMA_Config cfg = {
        .channel   = 0,
        .direction = SYN_DMA_MEM_TO_MEM,
        .width     = SYN_DMA_WIDTH_32,
        .src_incr  = true,
        .dst_incr  = true,
        .callback  = dma_cb,
        .user_data = (void *)0xCAFE,
    };
    TEST_ASSERT_EQUAL(SYN_OK, syn_port_dma_init(&cfg));
    TEST_ASSERT_TRUE(mock_dma[0].initialized);

    uint8_t src[64], dst[64];
    TEST_ASSERT_EQUAL(SYN_OK, syn_port_dma_start(0, src, dst, 16));
    TEST_ASSERT_TRUE(syn_port_dma_busy(0));
    TEST_ASSERT_EQUAL(16, syn_port_dma_remaining(0));
    TEST_ASSERT_EQUAL(1, mock_dma_start_count);
}

void test_dma_busy_rejects_second_start(void)
{
    SYN_DMA_Config cfg = {
        .channel = 1, .direction = SYN_DMA_MEM_TO_MEM,
        .width = SYN_DMA_WIDTH_8, .callback = NULL,
    };
    syn_port_dma_init(&cfg);
    uint8_t buf[4];
    syn_port_dma_start(1, buf, buf, 4);
    TEST_ASSERT_EQUAL(SYN_BUSY, syn_port_dma_start(1, buf, buf, 4));
}

void test_dma_stop(void)
{
    SYN_DMA_Config cfg = {
        .channel = 2, .direction = SYN_DMA_PERIPH_TO_MEM,
        .width = SYN_DMA_WIDTH_16, .callback = NULL,
    };
    syn_port_dma_init(&cfg);
    uint8_t buf[8];
    syn_port_dma_start(2, buf, buf, 8);
    TEST_ASSERT_TRUE(syn_port_dma_busy(2));

    syn_port_dma_stop(2);
    TEST_ASSERT_FALSE(syn_port_dma_busy(2));
    TEST_ASSERT_EQUAL(0, syn_port_dma_remaining(2));
    TEST_ASSERT_EQUAL(1, mock_dma_stop_count);
}

void test_dma_callback_fires(void)
{
    callback_count = 0;
    SYN_DMA_Config cfg = {
        .channel = 0, .direction = SYN_DMA_MEM_TO_MEM,
        .width = SYN_DMA_WIDTH_8, .callback = dma_cb,
        .user_data = (void *)0xBEEF,
    };
    syn_port_dma_init(&cfg);
    uint8_t buf[4];
    syn_port_dma_start(0, buf, buf, 4);

    mock_dma_complete(0, SYN_OK);

    TEST_ASSERT_EQUAL(1, callback_count);
    TEST_ASSERT_EQUAL(0, callback_channel);
    TEST_ASSERT_EQUAL(SYN_OK, callback_result);
    TEST_ASSERT_EQUAL_PTR((void *)0xBEEF, callback_ctx);
    TEST_ASSERT_FALSE(syn_port_dma_busy(0));
}

void test_dma_callback_error(void)
{
    callback_count = 0;
    SYN_DMA_Config cfg = {
        .channel = 0, .direction = SYN_DMA_MEM_TO_MEM,
        .width = SYN_DMA_WIDTH_8, .callback = dma_cb,
    };
    syn_port_dma_init(&cfg);
    uint8_t buf[4];
    syn_port_dma_start(0, buf, buf, 4);

    mock_dma_complete(0, SYN_ERROR);
    TEST_ASSERT_EQUAL(SYN_ERROR, callback_result);
}

void test_dma_invalid_channel(void)
{
    TEST_ASSERT_FALSE(syn_port_dma_busy(99));
    TEST_ASSERT_EQUAL(0, syn_port_dma_remaining(99));
    TEST_ASSERT_EQUAL(SYN_ERROR, syn_port_dma_stop(99));
}

void test_dma_uninit_start_fails(void)
{
    /* Channel 3 was never initialized */
    uint8_t buf[4] = {0};
    TEST_ASSERT_EQUAL(SYN_ERROR, syn_port_dma_start(3, buf, buf, 4));
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

void run_dma_tests(void)
{
    RUN_TEST(test_dma_init_and_start);
    RUN_TEST(test_dma_busy_rejects_second_start);
    RUN_TEST(test_dma_stop);
    RUN_TEST(test_dma_callback_fires);
    RUN_TEST(test_dma_callback_error);
    RUN_TEST(test_dma_invalid_channel);
    RUN_TEST(test_dma_uninit_start_fails);
}
