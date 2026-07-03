/**
 * @file test_sntp.c
 * @brief Unit tests for syn_sntp (Simple NTP client).
 *
 * Tests init, query parsing, epoch calculation, and edge cases
 * using the mock UDP socket infrastructure.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/net/syn_sntp.h"
#include <string.h>

/* ── Helpers ────────────────────────────────────────────────────────────── */

/**
 * Build a minimal valid NTP server response packet.
 * Only the fields checked by syn_sntp_query are set:
 *   - Byte 0:  LI=0, VN=4, Mode=4 (server)
 *   - Byte 1:  Stratum (non-zero)
 *   - Bytes 40-43: Transmit timestamp seconds (big-endian, NTP epoch)
 *   - Bytes 44-47: Transmit timestamp fraction (big-endian)
 */
static void build_ntp_response(uint8_t *pkt, uint32_t unix_epoch_s,
                                uint32_t frac)
{
    memset(pkt, 0, SYN_SNTP_PACKET_SIZE);
    pkt[0] = 0x24;   /* LI=0, VN=4, Mode=4 (server) */
    pkt[1] = 2;       /* Stratum 2 */

    /* Convert Unix epoch → NTP epoch */
    uint32_t ntp_s = unix_epoch_s + SYN_SNTP_EPOCH_OFFSET;

    pkt[40] = (uint8_t)(ntp_s >> 24);
    pkt[41] = (uint8_t)(ntp_s >> 16);
    pkt[42] = (uint8_t)(ntp_s >>  8);
    pkt[43] = (uint8_t)(ntp_s);
    pkt[44] = (uint8_t)(frac >> 24);
    pkt[45] = (uint8_t)(frac >> 16);
    pkt[46] = (uint8_t)(frac >>  8);
    pkt[47] = (uint8_t)(frac);
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

static void test_sntp_init(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {216, 239, 35, 0}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    TEST_ASSERT_FALSE(sntp.synced);
    TEST_ASSERT_EQUAL_UINT32(0, sntp.epoch_s);
    TEST_ASSERT_EQUAL_UINT32(3600, sntp.sync_interval_s);
    TEST_ASSERT_EQUAL_UINT16(123, sntp.server.port);
    TEST_ASSERT_EQUAL_UINT8(216, sntp.server.ip[0]);
}

static void test_sntp_not_synced_returns_zero(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    TEST_ASSERT_EQUAL_UINT32(0, syn_sntp_get_epoch_s(&sntp));
    TEST_ASSERT_EQUAL_UINT32(0, syn_sntp_get_epoch_ns(&sntp));
    TEST_ASSERT_FALSE(syn_sntp_is_synced(&sntp));
}

static void test_sntp_query_success(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    /* Set up mock response: Unix epoch = 1700000000 (Nov 14, 2023) */
    uint8_t resp[SYN_SNTP_PACKET_SIZE];
    build_ntp_response(resp, 1700000000UL, 0);

    SYN_SockAddr from = { .ip = {1, 2, 3, 4}, .port = 123 };
    mock_udp_set_response(resp, SYN_SNTP_PACKET_SIZE, &from);

    mock_tick_ms = 5000;  /* Simulate 5s uptime at sync time */

    SYN_Status status = syn_sntp_query(&sntp);

    TEST_ASSERT_EQUAL(SYN_OK, status);
    TEST_ASSERT_TRUE(syn_sntp_is_synced(&sntp));
    TEST_ASSERT_EQUAL_UINT32(1700000000UL, sntp.epoch_s);
    TEST_ASSERT_EQUAL_UINT32(5000, sntp.sync_tick_ms);
}

static void test_sntp_get_epoch_advances_with_tick(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    uint8_t resp[SYN_SNTP_PACKET_SIZE];
    build_ntp_response(resp, 1700000000UL, 0);
    SYN_SockAddr from = { .ip = {1, 2, 3, 4}, .port = 123 };
    mock_udp_set_response(resp, SYN_SNTP_PACKET_SIZE, &from);

    mock_tick_ms = 10000;
    syn_sntp_query(&sntp);

    /* Advance tick by 5 seconds */
    mock_tick_ms = 15000;
    TEST_ASSERT_EQUAL_UINT32(1700000005UL, syn_sntp_get_epoch_s(&sntp));

    /* Advance by 60 seconds total */
    mock_tick_ms = 70000;
    TEST_ASSERT_EQUAL_UINT32(1700000060UL, syn_sntp_get_epoch_s(&sntp));
}

static void test_sntp_get_epoch_ns(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    uint8_t resp[SYN_SNTP_PACKET_SIZE];
    build_ntp_response(resp, 1700000000UL, 0);
    SYN_SockAddr from = { .ip = {1, 2, 3, 4}, .port = 123 };
    mock_udp_set_response(resp, SYN_SNTP_PACKET_SIZE, &from);

    mock_tick_ms = 0;
    syn_sntp_query(&sntp);

    /* 500ms elapsed → 500,000,000 ns */
    mock_tick_ms = 500;
    TEST_ASSERT_EQUAL_UINT32(500000000UL, syn_sntp_get_epoch_ns(&sntp));

    /* 1500ms → sub-second is 500ms → 500,000,000 ns */
    mock_tick_ms = 1500;
    TEST_ASSERT_EQUAL_UINT32(500000000UL, syn_sntp_get_epoch_ns(&sntp));
}

static void test_sntp_query_timeout(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    /* No response data → recvfrom returns 0 */
    /* mock_udp_rx_len is 0 by default */

    SYN_Status status = syn_sntp_query(&sntp);

    TEST_ASSERT_EQUAL(SYN_TIMEOUT, status);
    TEST_ASSERT_FALSE(syn_sntp_is_synced(&sntp));
}

static void test_sntp_query_socket_fail(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    mock_udp_open_ok = false;

    SYN_Status status = syn_sntp_query(&sntp);

    TEST_ASSERT_EQUAL(SYN_ERROR, status);
    TEST_ASSERT_FALSE(syn_sntp_is_synced(&sntp));
}

static void test_sntp_query_bad_mode(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    /* Build response with wrong mode (Mode=3 = client, should be 4 or 5) */
    uint8_t resp[SYN_SNTP_PACKET_SIZE];
    build_ntp_response(resp, 1700000000UL, 0);
    resp[0] = 0x23;  /* Mode=3 (client) — invalid for a response */

    SYN_SockAddr from = { .ip = {1, 2, 3, 4}, .port = 123 };
    mock_udp_set_response(resp, SYN_SNTP_PACKET_SIZE, &from);

    SYN_Status status = syn_sntp_query(&sntp);

    TEST_ASSERT_EQUAL(SYN_ERROR, status);
}

static void test_sntp_query_kiss_of_death(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    uint8_t resp[SYN_SNTP_PACKET_SIZE];
    build_ntp_response(resp, 1700000000UL, 0);
    resp[1] = 0;  /* Stratum 0 = kiss-of-death */

    SYN_SockAddr from = { .ip = {1, 2, 3, 4}, .port = 123 };
    mock_udp_set_response(resp, SYN_SNTP_PACKET_SIZE, &from);

    SYN_Status status = syn_sntp_query(&sntp);

    TEST_ASSERT_EQUAL(SYN_ERROR, status);
}

static void test_sntp_query_sendto_fail(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    mock_udp_sendto_fail = true;

    SYN_Status status = syn_sntp_query(&sntp);

    TEST_ASSERT_EQUAL(SYN_ERROR, status);
}

static void test_sntp_request_packet_format(void)
{
    SYN_SNTP sntp;
    SYN_SockAddr server = { .ip = {1, 2, 3, 4}, .port = 123 };

    syn_sntp_init(&sntp, &server, 3600);

    /* Provide response so query completes */
    uint8_t resp[SYN_SNTP_PACKET_SIZE];
    build_ntp_response(resp, 1700000000UL, 0);
    SYN_SockAddr from = { .ip = {1, 2, 3, 4}, .port = 123 };
    mock_udp_set_response(resp, SYN_SNTP_PACKET_SIZE, &from);

    syn_sntp_query(&sntp);

    /* Verify the request packet that was sent */
    TEST_ASSERT_EQUAL(SYN_SNTP_PACKET_SIZE, mock_udp_tx_len);
    TEST_ASSERT_EQUAL_HEX8(0x23, mock_udp_tx_buf[0]); /* LI=0, VN=4, Mode=3 */

    /* Rest of request should be zeros */
    for (int i = 1; i < SYN_SNTP_PACKET_SIZE; i++) {
        TEST_ASSERT_EQUAL_HEX8(0, mock_udp_tx_buf[i]);
    }
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

void run_sntp_tests(void)
{
    RUN_TEST(test_sntp_init);
    RUN_TEST(test_sntp_not_synced_returns_zero);
    RUN_TEST(test_sntp_query_success);
    RUN_TEST(test_sntp_get_epoch_advances_with_tick);
    RUN_TEST(test_sntp_get_epoch_ns);
    RUN_TEST(test_sntp_query_timeout);
    RUN_TEST(test_sntp_query_socket_fail);
    RUN_TEST(test_sntp_query_bad_mode);
    RUN_TEST(test_sntp_query_kiss_of_death);
    RUN_TEST(test_sntp_query_sendto_fail);
    RUN_TEST(test_sntp_request_packet_format);
}
