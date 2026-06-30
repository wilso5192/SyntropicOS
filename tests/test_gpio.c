#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/drivers/syn_gpio.h"

/** init_multiple: success path */
static void test_gpio_init_multiple(void)
{
    SYN_GPIO_Pin pins[] = { 0, 1, 2 };
    SYN_Status st = syn_gpio_init_multiple(pins, 3, SYN_GPIO_OUTPUT);
    TEST_ASSERT_EQUAL(SYN_OK, st);
}

/** init_multiple: count=0 */
static void test_gpio_init_multiple_zero(void)
{
    SYN_Status st = syn_gpio_init_multiple(NULL, 0, SYN_GPIO_OUTPUT);
    TEST_ASSERT_EQUAL(SYN_OK, st);
}

/** init_multiple: failure on second pin (pin >= 32 triggers error) */
static void test_gpio_init_multiple_fail(void)
{
    SYN_GPIO_Pin pins[] = { 0, 99 }; /* 99 >= 32 → SYN_INVALID_PARAM */
    SYN_Status st = syn_gpio_init_multiple(pins, 2, SYN_GPIO_OUTPUT);
    TEST_ASSERT_NOT_EQUAL(SYN_OK, st);
}

/** write_multiple: success path */
static void test_gpio_write_multiple(void)
{
    SYN_GPIO_Pin pins[] = { 0, 1 };
    SYN_Status st = syn_gpio_write_multiple(pins, 2, SYN_GPIO_HIGH);
    TEST_ASSERT_EQUAL(SYN_OK, st);
}

/** write_multiple: count=0 */
static void test_gpio_write_multiple_zero(void)
{
    SYN_Status st = syn_gpio_write_multiple(NULL, 0, SYN_GPIO_HIGH);
    TEST_ASSERT_EQUAL(SYN_OK, st);
}

/** write_multiple: failure on bad pin */
static void test_gpio_write_multiple_fail(void)
{
    SYN_GPIO_Pin pins[] = { 0, 99 };
    SYN_Status st = syn_gpio_write_multiple(pins, 2, SYN_GPIO_HIGH);
    TEST_ASSERT_NOT_EQUAL(SYN_OK, st);
}

void run_gpio_tests(void)
{
    RUN_TEST(test_gpio_init_multiple);
    RUN_TEST(test_gpio_init_multiple_zero);
    RUN_TEST(test_gpio_init_multiple_fail);
    RUN_TEST(test_gpio_write_multiple);
    RUN_TEST(test_gpio_write_multiple_zero);
    RUN_TEST(test_gpio_write_multiple_fail);
}
