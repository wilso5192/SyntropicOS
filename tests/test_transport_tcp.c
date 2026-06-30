#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/net/syn_transport_tcp.h"
#include <string.h>

void test_transport_tcp_send(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;

    uint8_t data[] = { 0xAA, 0xBB, 0xCC };
    TEST_ASSERT_TRUE(syn_transport_send(&t, data, sizeof(data)));

    /* Verify 2-byte length + payload */
    TEST_ASSERT_EQUAL_UINT32(5, mock_sock_tx_len);
    TEST_ASSERT_EQUAL_UINT8(0x00, mock_sock_tx_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, mock_sock_tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, mock_sock_tx_buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, mock_sock_tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, mock_sock_tx_buf[4]);
}

void test_transport_tcp_recv_full(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;

    uint8_t rx_data[] = { 0x00, 0x04, 0x11, 0x22, 0x33, 0x44 };
    mock_sock_set_response(rx_data, sizeof(rx_data));

    uint8_t out[16];
    size_t out_len = 0;
    TEST_ASSERT_TRUE(syn_transport_recv(&t, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT32(4, out_len);
    TEST_ASSERT_EQUAL_UINT8(0x11, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x33, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0x44, out[3]);
}

void test_transport_tcp_recv_fragmented(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;

    uint8_t out[16];
    size_t out_len = 0;

    /* 1. Feed length MSB */
    uint8_t chunk1[] = { 0x00 };
    mock_sock_set_response(chunk1, 1);
    TEST_ASSERT_FALSE(syn_transport_recv(&t, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT8(1, tcp.state);

    /* 2. Feed length LSB */
    uint8_t chunk2[] = { 0x03 };
    mock_sock_set_response(chunk2, 1);
    TEST_ASSERT_FALSE(syn_transport_recv(&t, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT8(2, tcp.state);
    TEST_ASSERT_EQUAL_UINT16(3, tcp.payload_len);

    /* 3. Feed partial payload */
    uint8_t chunk3[] = { 0xAA, 0xBB };
    mock_sock_set_response(chunk3, 2);
    TEST_ASSERT_FALSE(syn_transport_recv(&t, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT8(2, tcp.state);
    TEST_ASSERT_EQUAL_UINT16(2, tcp.bytes_read);

    /* 4. Feed final payload byte */
    uint8_t chunk4[] = { 0xCC };
    mock_sock_set_response(chunk4, 1);
    TEST_ASSERT_TRUE(syn_transport_recv(&t, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT32(3, out_len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0, tcp.state);
}

/** send fails on header write — exercises line 36 */
static void test_transport_tcp_send_header_fail(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;
    mock_sock_send_fail = true; /* fail immediately */

    uint8_t data[] = { 0x01 };
    bool ok = syn_transport_send(&t, data, sizeof(data));
    TEST_ASSERT_FALSE(ok);
    mock_sock_send_fail = false;
}

/** send fails on payload write — exercises line 41 */
static void test_transport_tcp_send_payload_fail(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;
    /* Fail after 2 bytes (the header) so the payload write fails */
    mock_sock_send_fail_after_bytes = 2;

    uint8_t data[] = { 0x01, 0x02 };
    bool ok = syn_transport_send(&t, data, sizeof(data));
    TEST_ASSERT_FALSE(ok);
    mock_sock_send_fail_after_bytes = -1;
}

/** recv: oversized payload length — exercises lines 84-85 */
static void test_transport_tcp_recv_oversized(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;

    /* Send a length header larger than the internal rx_buf */
    uint8_t rx[] = { 0xFF, 0xFF }; /* length = 65535, far exceeds rx_buf */
    mock_sock_set_response(rx, sizeof(rx));

    uint8_t out[16];
    size_t out_len;
    /* State 0: read high byte */
    bool ok = syn_transport_recv(&t, out, sizeof(out), &out_len);
    TEST_ASSERT_FALSE(ok);
    /* State 1→oversized: resets to 0 */
    mock_sock_set_response(rx + 1, 1);
    ok = syn_transport_recv(&t, out, sizeof(out), &out_len);
    TEST_ASSERT_FALSE(ok); /* oversized → reset to state 0 */
}

/** recv: zero-length (empty) packet — exercises lines 92-93 */
static void test_transport_tcp_recv_empty_packet(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;

    /* Length = 0x0000: feed both bytes at once so state machine gets high+low */
    uint8_t rx[] = { 0x00, 0x00 };
    mock_sock_set_response(rx, sizeof(rx));

    uint8_t out[16];
    size_t out_len = 99;

    /* Call recv 3 times to walk through states 0→1→2(empty→done) */
    bool ok = false;
    for (int i = 0; i < 4 && !ok; i++) {
        ok = syn_transport_recv(&t, out, sizeof(out), &out_len);
    }
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0, out_len);
}
/** recv: completed packet larger than output buffer — exercises lines 109-110 */
static void test_transport_tcp_recv_outbuf_too_small(void)
{
    SYN_Transport t;
    SYN_TransportTcp tcp;
    syn_transport_tcp_init(&t, &tcp, 1);
    mock_sock_connected = true;

    /* Frame a 5-byte payload: length header = 0x0005, then 5 data bytes */
    uint8_t rx[] = { 0x00, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    mock_sock_set_response(rx, sizeof(rx));

    /* But provide a 2-byte output buffer — smaller than payload */
    uint8_t out[2];
    size_t out_len;
    bool ok = false;
    for (int i = 0; i < 10 && !ok; i++) {
        ok = syn_transport_recv(&t, out, sizeof(out), &out_len);
    }
    /* Completed packet but output buffer too small → returns false (lines 109-110) */
    TEST_ASSERT_FALSE(ok);
}

void run_transport_tcp_tests(void)
{
    RUN_TEST(test_transport_tcp_send);
    RUN_TEST(test_transport_tcp_recv_full);
    RUN_TEST(test_transport_tcp_recv_fragmented);
    RUN_TEST(test_transport_tcp_send_header_fail);
    RUN_TEST(test_transport_tcp_send_payload_fail);
    RUN_TEST(test_transport_tcp_recv_oversized);
    RUN_TEST(test_transport_tcp_recv_empty_packet);
    RUN_TEST(test_transport_tcp_recv_outbuf_too_small);
}
