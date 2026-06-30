/**
 * @file test_stepper.c
 * @brief Unity tests for syn_stepper.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/motor/syn_stepper.h"

static void test_stepper(void)
{

    mock_tick_ms = 0;

    SYN_Stepper stepper;
    syn_stepper_init(&stepper, 5, 6);
    syn_stepper_set_speed(&stepper, 100, 1000);

    TEST_ASSERT_FALSE(syn_stepper_is_moving(&stepper));
    TEST_ASSERT_EQUAL_INT(0, syn_stepper_position(&stepper));

    /* Move 10 steps forward */
    syn_stepper_move(&stepper, 10);
    TEST_ASSERT_TRUE(syn_stepper_is_moving(&stepper));

    /* Tick until complete */
    int ticks = 0;
    while (syn_stepper_is_moving(&stepper) && ticks < 10000) {
        mock_tick_advance(1);
        syn_stepper_tick(&stepper);
        ticks++;
    }
    TEST_ASSERT_FALSE(syn_stepper_is_moving(&stepper));
    TEST_ASSERT_EQUAL_INT(10, syn_stepper_position(&stepper));

    /* Move to absolute position */
    syn_stepper_move_to(&stepper, 0);
    while (syn_stepper_is_moving(&stepper) && ticks < 20000) {
        mock_tick_advance(1);
        syn_stepper_tick(&stepper);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT(0, syn_stepper_position(&stepper));

    /* Emergency stop */
    syn_stepper_move(&stepper, 1000);
    mock_tick_advance(5);
    syn_stepper_tick(&stepper);
    syn_stepper_stop(&stepper);
    TEST_ASSERT_FALSE(syn_stepper_is_moving(&stepper));

    /* Set position */
    syn_stepper_set_position(&stepper, 500);
    TEST_ASSERT_EQUAL_INT(500, syn_stepper_position(&stepper));
}

/** syn_stepper_set_enable_pin + syn_stepper_enable — lines 51-57, 194-203 */
static void test_stepper_enable_pin(void)
{
    mock_tick_ms = 0;
    SYN_Stepper s;
    syn_stepper_init(&s, 5, 6);
    syn_stepper_set_speed(&s, 100, 1000);

    /* Configure enable pin (active_low=false) and toggle */
    syn_stepper_set_enable_pin(&s, 7, false);
    syn_stepper_enable(&s, true);
    TEST_ASSERT_EQUAL(SYN_GPIO_HIGH, mock_gpio_states[7]);
    syn_stepper_enable(&s, false);
    TEST_ASSERT_EQUAL(SYN_GPIO_LOW, mock_gpio_states[7]);

    /* Configure enable pin (active_low=true) — exercises line 28, 200-201 */
    syn_stepper_set_enable_pin(&s, 8, true);
    syn_stepper_enable(&s, true);
    /* active_low: enable=true → write LOW */
    TEST_ASSERT_EQUAL(SYN_GPIO_LOW, mock_gpio_states[8]);
    syn_stepper_enable(&s, false);
    TEST_ASSERT_EQUAL(SYN_GPIO_HIGH, mock_gpio_states[8]);
}

/** syn_stepper_tick while IDLE — exercises case SYN_STEPPER_IDLE (line 145) */
static void test_stepper_tick_idle(void)
{
    mock_tick_ms = 0;
    SYN_Stepper s;
    syn_stepper_init(&s, 5, 6);
    syn_stepper_set_speed(&s, 100, 1000);

    /* Tick before any move — should be a no-op */
    syn_stepper_tick(&s);
    TEST_ASSERT_FALSE(syn_stepper_is_moving(&s));
    TEST_ASSERT_EQUAL_INT(0, syn_stepper_position(&s));
}

/** Short move with high accel — accel_steps clamped to abs_steps/2 (line 93) */
static void test_stepper_accel_clamp(void)
{
    mock_tick_ms = 0;
    SYN_Stepper s;
    syn_stepper_init(&s, 5, 6);
    /* High accel relative to speed: v²/(2a) would exceed half of 3 steps */
    syn_stepper_set_speed(&s, 1000, 10000); /* max_speed=1000 steps/s, accel=10000 steps/s² */
    syn_stepper_move(&s, 3);              /* only 3 steps — accel_steps would be 50, clamped to 1 */
    TEST_ASSERT_TRUE(syn_stepper_is_moving(&s));

    int ticks = 0;
    while (syn_stepper_is_moving(&s) && ticks < 50000) {
        mock_tick_advance(1);
        syn_stepper_tick(&s);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT(3, syn_stepper_position(&s));
}

/** dir_invert — exercises line 28 (direction flip) */
static void test_stepper_dir_invert(void)
{
    mock_tick_ms = 0;
    SYN_Stepper s;
    syn_stepper_init(&s, 5, 6);
    syn_stepper_set_speed(&s, 100, 1000);
    s.dir_invert = true; /* force direction inversion */

    /* Move forward — set_direction called with forward=true but inverted */
    syn_stepper_move(&s, 5);
    TEST_ASSERT_TRUE(syn_stepper_is_moving(&s));

    int ticks = 0;
    while (syn_stepper_is_moving(&s) && ticks < 50000) {
        mock_tick_advance(1);
        syn_stepper_tick(&s);
        ticks++;
    }
    /* position should still be +5 regardless of pin direction */
    TEST_ASSERT_EQUAL_INT(5, syn_stepper_position(&s));
}

void run_stepper_tests(void)
{
    RUN_TEST(test_stepper);
    RUN_TEST(test_stepper_enable_pin);
    RUN_TEST(test_stepper_tick_idle);
    RUN_TEST(test_stepper_accel_clamp);
    RUN_TEST(test_stepper_dir_invert);
}
