/**
 * @file test_exti.c
 * @brief Unity tests for syn_exti — full coverage (adds enable/disable/capacity).
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/drivers/syn_exti.h"

static int exti_fire_count = 0;
static SYN_GPIO_Pin exti_last_pin = 255;

static void exti_callback(SYN_GPIO_Pin pin, void *ctx)
{
    (void)ctx;
    exti_fire_count++;
    exti_last_pin = pin;
}

static int exti_ctx_value = 0;
static void exti_callback_ctx(SYN_GPIO_Pin pin, void *ctx)
{
    (void)pin;
    exti_ctx_value = *(int *)ctx;
}

static void test_exti(void)
{
    exti_fire_count = 0;
    exti_last_pin = 255;
    exti_ctx_value = 0;

    syn_exti_init();
    TEST_ASSERT_EQUAL_INT(0, syn_exti_count());

    /* Register pin 3 */
    SYN_Status st = syn_exti_register(3, SYN_EXTI_FALLING,
                                        exti_callback, NULL);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_EQUAL_INT(1, syn_exti_count());

    /* Simulate ISR firing */
    syn_exti_irq_handler(3);
    TEST_ASSERT_EQUAL_INT(1, exti_fire_count);
    TEST_ASSERT_EQUAL_INT(3, exti_last_pin);

    /* Fire again */
    syn_exti_irq_handler(3);
    TEST_ASSERT_EQUAL_INT(2, exti_fire_count);

    /* Unregistered pin — no crash */
    syn_exti_irq_handler(99);
    TEST_ASSERT_EQUAL_INT(2, exti_fire_count);

    /* Register with context */
    static int val = 42;
    syn_exti_register(7, SYN_EXTI_RISING, exti_callback_ctx, &val);
    syn_exti_irq_handler(7);
    TEST_ASSERT_EQUAL_INT(42, exti_ctx_value);

    /* Unregister */
    syn_exti_unregister(3);
    syn_exti_irq_handler(3);
    TEST_ASSERT_EQUAL_INT(2, exti_fire_count);

    /* Re-register same pin (update in place) */
    syn_exti_register(3, SYN_EXTI_BOTH, exti_callback, NULL);
    syn_exti_irq_handler(3);
    TEST_ASSERT_EQUAL_INT(3, exti_fire_count);
}

/** syn_exti_enable / syn_exti_disable — exercises port enable/disable calls */
static void test_exti_enable_disable(void)
{
    syn_exti_init();
    exti_fire_count = 0;

    /* Register a pin first */
    syn_exti_register(5, SYN_EXTI_RISING, exti_callback, NULL);

    /* Enable — should call syn_port_exti_enable (no-op in mock) */
    syn_exti_enable(5);
    /* Verify handler still works after enable */
    syn_exti_irq_handler(5);
    TEST_ASSERT_EQUAL_INT(1, exti_fire_count);

    /* Disable */
    syn_exti_disable(5);

    /* Enable on an unknown pin — should not crash */
    syn_exti_enable(99);

    /* Disable on an unknown pin — should not crash */
    syn_exti_disable(99);
}

/** Capacity limit — filling all slots returns SYN_ERROR on next register */
static void test_exti_capacity_full(void)
{
    syn_exti_init();

    /* Fill up to SYN_EXTI_MAX_PINS (default 16) */
    int registered = 0;
    for (int i = 0; i < 16; i++) {
        SYN_Status st = syn_exti_register((SYN_GPIO_Pin)i,
                                           SYN_EXTI_RISING,
                                           exti_callback, NULL);
        if (st == SYN_OK) registered++;
    }
    TEST_ASSERT_EQUAL_INT(16, registered);
    TEST_ASSERT_EQUAL_INT(16, syn_exti_count());

    /* One more should fail */
    SYN_Status overflow = syn_exti_register(100, SYN_EXTI_RISING,
                                              exti_callback, NULL);
    TEST_ASSERT_EQUAL(SYN_ERROR, overflow);
}

void run_exti_tests(void)
{
    RUN_TEST(test_exti);
    RUN_TEST(test_exti_enable_disable);
    RUN_TEST(test_exti_capacity_full);
}
