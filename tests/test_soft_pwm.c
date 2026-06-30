/**
 * @file test_soft_pwm.c
 * @brief Unity tests for syn_soft_pwm.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/output/syn_soft_pwm.h"

static void test_soft_pwm(void)
{

    mock_gpio_states[2] = 0;

    SYN_SoftPWM pwm;
    syn_soft_pwm_init(&pwm, 2, 10); /* resolution = 10 steps */
    syn_soft_pwm_set_duty(&pwm, 3);  /* 30% duty */

    int on_count = 0;
    int i;
    for (i = 0; i < 10; i++) {
        syn_soft_pwm_tick(&pwm);
        if (mock_gpio_states[2] == SYN_GPIO_HIGH) on_count++;
    }
    TEST_ASSERT_EQUAL_INT(3, on_count);

    /* 0% duty */
    syn_soft_pwm_set_duty(&pwm, 0);
    on_count = 0;
    for (i = 0; i < 10; i++) {
        syn_soft_pwm_tick(&pwm);
        if (mock_gpio_states[2] == SYN_GPIO_HIGH) on_count++;
    }
    TEST_ASSERT_EQUAL_INT(0, on_count);

    /* 100% duty */
    syn_soft_pwm_set_duty(&pwm, 10);
    on_count = 0;
    for (i = 0; i < 10; i++) {
        syn_soft_pwm_tick(&pwm);
        if (mock_gpio_states[2] == SYN_GPIO_HIGH) on_count++;
    }
    TEST_ASSERT_EQUAL_INT(10, on_count);

    /* Percent API */
    syn_soft_pwm_set_percent(&pwm, 50);
    TEST_ASSERT_EQUAL_INT(5, syn_soft_pwm_get_duty(&pwm));
}

/** duty > resolution clamps — exercises line 38 */
static void test_soft_pwm_duty_clamp(void)
{
    SYN_SoftPWM pwm;
    syn_soft_pwm_init(&pwm, 2, 10);
    syn_soft_pwm_set_duty(&pwm, 99); /* way over resolution */
    TEST_ASSERT_EQUAL_INT(10, pwm.duty); /* clamped to resolution */
}

/** active_low inversion — exercises line 70 */
static void test_soft_pwm_active_low(void)
{
    SYN_SoftPWM pwm;
    syn_soft_pwm_init(&pwm, 3, 10);
    pwm.active_high = false; /* active-low mode */
    syn_soft_pwm_set_duty(&pwm, 5); /* 50% */

    /* At counter=0: counter(0) < duty(5) → on=true → active_low → GPIO_LOW */
    syn_soft_pwm_tick(&pwm);
    TEST_ASSERT_EQUAL_INT(SYN_GPIO_LOW, mock_gpio_states[3]);

    /* Advance past duty */
    for (int i = 0; i < 5; i++) syn_soft_pwm_tick(&pwm);
    /* counter(5) >= duty(5) → on=false → active_low → GPIO_HIGH */
    TEST_ASSERT_EQUAL_INT(SYN_GPIO_HIGH, mock_gpio_states[3]);
}

/** syn_soft_pwm_service multi-channel — exercises lines 80-87 */
static void test_soft_pwm_service(void)
{
    SYN_SoftPWM channels[3];
    syn_soft_pwm_init(&channels[0], 4, 10);
    syn_soft_pwm_init(&channels[1], 5, 10);
    syn_soft_pwm_init(&channels[2], 6, 10);
    syn_soft_pwm_set_duty(&channels[0], 5);
    syn_soft_pwm_set_duty(&channels[1], 2);
    syn_soft_pwm_set_duty(&channels[2], 8);

    /* Service all 3 channels in one call */
    syn_soft_pwm_service(channels, 3);
    /* All counters should have advanced by 1 */
    TEST_ASSERT_EQUAL_INT(1, channels[0].counter);
    TEST_ASSERT_EQUAL_INT(1, channels[1].counter);
    TEST_ASSERT_EQUAL_INT(1, channels[2].counter);
}

void run_soft_pwm_tests(void)
{
    RUN_TEST(test_soft_pwm);
    RUN_TEST(test_soft_pwm_duty_clamp);
    RUN_TEST(test_soft_pwm_active_low);
    RUN_TEST(test_soft_pwm_service);
}
