/**
 * @file test_fmt.c
 * @brief Unity tests for syn_fmt.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/util/syn_fmt.h"

static void test_fmt(void)
{

    char buf[64];
    size_t n;

    /* Signed integer */
    n = syn_fmt_int(buf, sizeof(buf), 12345);
    TEST_ASSERT_EQUAL_INT(5, n);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "12345"));

    n = syn_fmt_int(buf, sizeof(buf), -42);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "-42"));

    n = syn_fmt_int(buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "0"));

    /* Unsigned */
    n = syn_fmt_uint(buf, sizeof(buf), 4294967295u);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "4294967295"));

    /* Hex */
    n = syn_fmt_hex(buf, sizeof(buf), 0xDEAD, 4);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "DEAD"));

    n = syn_fmt_hex(buf, sizeof(buf), 0x0A, 4);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "000A"));

    /* Q16.16 */
    int32_t q = Q16_FROM_FRAC(355, 113); /* ≈ π ≈ 3.141 */
    n = syn_fmt_q16(buf, sizeof(buf), q, 3);
    TEST_ASSERT_EQUAL('3', buf[0]);
    TEST_ASSERT_EQUAL('.', buf[1]);
    /* 355/113 ≈ 3.14159 → should be "3.141" */
    TEST_ASSERT_EQUAL('1', buf[2]);

    /* Negative Q16 */
    n = syn_fmt_q16(buf, sizeof(buf), Q16_FROM_INT(-7), 2);
    TEST_ASSERT_EQUAL('-', buf[0]);
    TEST_ASSERT_EQUAL('7', buf[1]);

    /* Fixed decimal */
    n = syn_fmt_fixed(buf, sizeof(buf), 12345, 3);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "12.345"));

    /* Hex dump */
    uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    n = syn_fmt_hexdump(buf, sizeof(buf), data, 4);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "DE AD BE EF"));

    /* Concat */
    const char *parts[] = { "Hello", " ", "World" };
    n = syn_fmt_concat(buf, sizeof(buf), parts, 3);
    TEST_ASSERT_EQUAL_INT(0, strcmp(buf, "Hello World"));

    /* Truncation safety */
    char tiny[4];
    syn_fmt_int(tiny, sizeof(tiny), 12345);
    TEST_ASSERT_EQUAL('\0', tiny[3]);

    (void)n;
}

/** syn_fmt_hex(0, 0) — exercises line 131 (min_digits forced to 1) */
static void test_fmt_hex_zero(void)
{
    char buf[16];
    size_t n = syn_fmt_hex(buf, sizeof(buf), 0, 0);
    TEST_ASSERT_EQUAL_STRING("0", buf);
    TEST_ASSERT_EQUAL_size_t(1, n);
}

/** syn_fmt_hex truncation — exercises line 146 */
static void test_fmt_hex_truncation(void)
{
    char buf[3]; /* only 2 chars + NUL */
    syn_fmt_hex(buf, sizeof(buf), 0xABCD, 0);
    TEST_ASSERT_EQUAL('\0', buf[2]); /* NUL-terminated */
}

/** syn_fmt_q16 with fractional-only value (<1.0) — exercises line 175 */
static void test_fmt_q16_fractional(void)
{
    char buf[32];
    /* 0.5 in Q16.16 = 0x00008000 */
    int32_t half = (int32_t)0x00008000;
    syn_fmt_q16(buf, sizeof(buf), half, 4);
    /* Should start with "0." */
    TEST_ASSERT_EQUAL('0', buf[0]);
    TEST_ASSERT_EQUAL('.', buf[1]);
}

/** syn_fmt_fixed with negative value — exercises lines 248-249 */
static void test_fmt_fixed_negative(void)
{
    char buf[32];
    syn_fmt_fixed(buf, sizeof(buf), -1234, 2);
    /* -1234 with 2 decimal places = "-12.34" */
    TEST_ASSERT_EQUAL('-', buf[0]);
    TEST_ASSERT_TRUE(strlen(buf) > 1);
}

/** syn_fmt_fixed with value < divisor (int_part == 0) — exercises line 264 */
static void test_fmt_fixed_small(void)
{
    char buf[32];
    /* val=5, places=2 → int_part=0, frac=05 → "0.05" */
    syn_fmt_fixed(buf, sizeof(buf), 5, 2);
    TEST_ASSERT_EQUAL_STRING("0.05", buf);
}

/** syn_fmt_uint truncation — exercises line 73 (buf full) */
static void test_fmt_str_truncation(void)
{
    char buf[4]; /* room for 3 chars + NUL */
    syn_fmt_uint(buf, sizeof(buf), 123456789u); /* 9 digits won't fit */
    TEST_ASSERT_EQUAL('\0', buf[3]);
    TEST_ASSERT_EQUAL('1', buf[0]);
    TEST_ASSERT_EQUAL('2', buf[1]);
    TEST_ASSERT_EQUAL('3', buf[2]);
}

/** syn_fmt_q16 truncation — exercises line 208 */
static void test_fmt_q16_truncation(void)
{
    char buf[4]; /* very small */
    /* 3.14159 in Q16.16 */
    int32_t pi = (int32_t)(3.14159 * 65536.0);
    syn_fmt_q16(buf, sizeof(buf), pi, 2);
    TEST_ASSERT_EQUAL('\0', buf[3]);
}

/** syn_fmt_hexdump truncation — exercises line 232 */
static void test_fmt_hexdump_truncation(void)
{
    char buf[4]; /* room for only 1 byte "XX" + space + NUL */
    uint8_t data[] = { 0xAB, 0xCD, 0xEF };
    syn_fmt_hexdump(buf, sizeof(buf), data, sizeof(data));
    TEST_ASSERT_EQUAL('\0', buf[3]);
}

/** syn_fmt_fixed truncation — exercises line 291 */
static void test_fmt_fixed_truncation(void)
{
    char buf[3]; /* tiny */
    syn_fmt_fixed(buf, sizeof(buf), 1234567, 2);
    TEST_ASSERT_EQUAL('\0', buf[2]);
}

/** syn_fmt_concat truncation — exercises line 313 */
static void test_fmt_concat_truncation(void)
{
    char buf[5];
    const char *parts[] = { "Hello", " ", "World" };
    syn_fmt_concat(buf, sizeof(buf), parts, 3);
    TEST_ASSERT_EQUAL('\0', buf[4]);
}

void run_fmt_tests(void)
{
    RUN_TEST(test_fmt);
    RUN_TEST(test_fmt_hex_zero);
    RUN_TEST(test_fmt_hex_truncation);
    RUN_TEST(test_fmt_q16_fractional);
    RUN_TEST(test_fmt_fixed_negative);
    RUN_TEST(test_fmt_fixed_small);
    RUN_TEST(test_fmt_str_truncation);
    RUN_TEST(test_fmt_q16_truncation);
    RUN_TEST(test_fmt_hexdump_truncation);
    RUN_TEST(test_fmt_fixed_truncation);
    RUN_TEST(test_fmt_concat_truncation);
}
