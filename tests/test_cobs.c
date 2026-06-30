/**
 * @file test_cobs.c
 * @brief Unity tests for syn_cobs.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/proto/syn_cobs.h"

static uint8_t cobs_rx_buf[256];
static size_t  cobs_rx_len = 0;

static void cobs_on_packet(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    memcpy(cobs_rx_buf, data, len);
    cobs_rx_len = len;
}

static void test_cobs(void)
{

    /* Encode / decode roundtrip */
    uint8_t orig[] = { 0x00, 0x11, 0x00, 0x00, 0x22, 0x33 };
    uint8_t encoded[16], decoded[16];
    size_t enc_len = syn_cobs_encode(orig, sizeof(orig), encoded);
    TEST_ASSERT_TRUE(enc_len > 0);

    size_t dec_len = syn_cobs_decode(encoded, enc_len, decoded);
    TEST_ASSERT_EQUAL(sizeof(orig), dec_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(orig, decoded, sizeof(orig)));

    /* Simple data (no zeros) */
    uint8_t simple[] = { 0x01, 0x02, 0x03 };
    enc_len = syn_cobs_encode(simple, 3, encoded);
    dec_len = syn_cobs_decode(encoded, enc_len, decoded);
    TEST_ASSERT_EQUAL_INT(3, dec_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(simple, decoded, 3));

    /* All zeros */
    uint8_t zeros[] = { 0x00, 0x00, 0x00 };
    enc_len = syn_cobs_encode(zeros, 3, encoded);
    dec_len = syn_cobs_decode(encoded, enc_len, decoded);
    TEST_ASSERT_EQUAL_INT(3, dec_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(zeros, decoded, 3));

    /* Streaming decoder */
    SYN_COBS_Decoder dec;
    uint8_t stream_buf[128];
    syn_cobs_decoder_init(&dec, stream_buf, sizeof(stream_buf),
                           cobs_on_packet, NULL);

    /* Feed the encoded packet byte-by-byte, then delimiter */
    enc_len = syn_cobs_encode(simple, 3, encoded);
    cobs_rx_len = 0;
    size_t i;
    for (i = 0; i < enc_len; i++) {
        syn_cobs_decoder_feed(&dec, encoded[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, cobs_rx_len);

    syn_cobs_decoder_feed(&dec, 0x00); /* delimiter */
    TEST_ASSERT_EQUAL_INT(3, cobs_rx_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(cobs_rx_buf, simple, 3));
}

/** Encode a 254-byte run — exercises lines 40-42 (max run boundary) */
static void test_cobs_max_run(void)
{
    /* 254 non-zero bytes causes a code=0xFF boundary flush */
    static uint8_t src[254];
    static uint8_t dst[256];
    for (int i = 0; i < 254; i++) src[i] = (uint8_t)(i + 1);

    size_t enc = syn_cobs_encode(src, sizeof(src), dst);
    TEST_ASSERT_TRUE(enc > 0);

    /* Decode back and verify */
    static uint8_t dec[254];
    size_t dec_len = syn_cobs_decode(dst, enc, dec);
    TEST_ASSERT_EQUAL_size_t(254, dec_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(src, dec, 254));
}

/** Decode with 0x00 as a code byte — exercises line 67 (invalid code=0) */
static void test_cobs_decode_zero_in_payload(void)
{
    /* A valid COBS frame starts with a non-zero code byte.
     * Here: code=1 (zero run of length 0, then write a zero), then code=0x00
     * which is invalid and should cause decode to return 0. */
    uint8_t bad[] = { 0x01, 0x00, 0x01 }; /* code=1 writes zero, then code=0 is invalid */
    uint8_t out[16];
    size_t n = syn_cobs_decode(bad, sizeof(bad), out);
    TEST_ASSERT_EQUAL_size_t(0, n); /* returns 0 on invalid code=0 */
}

/** Decode with run longer than remaining data — exercises line 72 (malformed) */
static void test_cobs_decode_malformed(void)
{
    uint8_t bad[] = { 0x05, 0x01 }; /* code=5 but only 1 byte follows */
    uint8_t out[16];
    size_t n = syn_cobs_decode(bad, sizeof(bad), out);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

/** Streaming decoder buffer overflow — exercises line 126 (discard frame) */
static int cobs_overflow_rx = 0;
static void on_cobs_overflow(const uint8_t *d, size_t l, void *c)
{ (void)d; (void)l; (void)c; cobs_overflow_rx++; }

static void test_cobs_decoder_overflow(void)
{
    static uint8_t dec_buf[4]; /* tiny buffer */
    cobs_overflow_rx = 0;

    SYN_COBS_Decoder dec;
    syn_cobs_decoder_init(&dec, dec_buf, sizeof(dec_buf), on_cobs_overflow, NULL);

    /* Feed 10 non-zero bytes — exceeds buffer of 4 */
    for (int i = 0; i < 10; i++) {
        syn_cobs_decoder_feed(&dec, (uint8_t)(i + 1));
    }
    /* idx should have been reset to 0 (overflow path) */
    TEST_ASSERT_EQUAL_INT(0, dec.idx);
}

/** syn_cobs_decoder_reset — exercises lines 131-135 */
static void test_cobs_decoder_reset(void)
{
    static uint8_t dec_buf[64];
    SYN_COBS_Decoder dec;
    syn_cobs_decoder_init(&dec, dec_buf, sizeof(dec_buf), NULL, NULL);

    syn_cobs_decoder_feed(&dec, 0x01);
    syn_cobs_decoder_feed(&dec, 0x02);
    TEST_ASSERT_EQUAL_INT(2, dec.idx);

    syn_cobs_decoder_reset(&dec);
    TEST_ASSERT_EQUAL_INT(0, dec.idx);
}

void run_cobs_tests(void)
{
    RUN_TEST(test_cobs);
    RUN_TEST(test_cobs_max_run);
    RUN_TEST(test_cobs_decode_zero_in_payload);
    RUN_TEST(test_cobs_decode_malformed);
    RUN_TEST(test_cobs_decoder_overflow);
    RUN_TEST(test_cobs_decoder_reset);
}
