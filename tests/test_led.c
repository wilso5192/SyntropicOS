/**
 * @file test_led.c
 * @brief Unity tests for syn_led.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/output/syn_led.h"

static void test_led_basic(void)
{
    mock_tick_ms = 0;
    mock_gpio_states[1] = 0;

    SYN_LED led;
    syn_led_init(&led, 1, SYN_LED_ACTIVE_HIGH);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));

    syn_led_on(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));
    TEST_ASSERT_EQUAL(SYN_GPIO_HIGH, mock_gpio_states[1]);
    syn_led_update(&led); /* covers case SYN_LED_MODE_ON */

    syn_led_off(&led);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));
    syn_led_update(&led); /* covers case SYN_LED_MODE_OFF */

    syn_led_toggle(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));
    syn_led_toggle(&led);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));

    /* Blink */
    syn_led_blink(&led, 100, 100);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    mock_tick_advance(100);
    syn_led_update(&led);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));

    mock_tick_advance(100);
    syn_led_update(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    /* Flash N times */
    syn_led_flash(&led, 50, 50, 2);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    mock_tick_advance(50);
    syn_led_update(&led);  /* on->off, remain=1 */
    TEST_ASSERT_FALSE(syn_led_is_on(&led));

    mock_tick_advance(50);
    syn_led_update(&led);  /* off->on for flash 2 */
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    mock_tick_advance(50);
    syn_led_update(&led);  /* on->off, remain=0, mode=OFF */
    TEST_ASSERT_FALSE(syn_led_is_on(&led));
}

static void test_led_active_low(void)
{
    mock_tick_ms = 0;
    mock_gpio_states[2] = 1;

    SYN_LED led;
    syn_led_init(&led, 2, SYN_LED_ACTIVE_LOW);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));
    TEST_ASSERT_EQUAL(SYN_GPIO_HIGH, mock_gpio_states[2]); // Initial off

    syn_led_on(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));
    TEST_ASSERT_EQUAL(SYN_GPIO_LOW, mock_gpio_states[2]); // Active low on

    syn_led_off(&led);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));
    TEST_ASSERT_EQUAL(SYN_GPIO_HIGH, mock_gpio_states[2]); // Active low off
}

static void test_led_service(void)
{
    mock_tick_ms = 0;
    SYN_LED leds[2];
    syn_led_init(&leds[0], 1, SYN_LED_ACTIVE_HIGH);
    syn_led_init(&leds[1], 2, SYN_LED_ACTIVE_HIGH);

    syn_led_blink(&leds[0], 100, 100);
    syn_led_blink(&leds[1], 50, 50);

    syn_led_service(leds, 2);
    TEST_ASSERT_TRUE(syn_led_is_on(&leds[0]));
    TEST_ASSERT_TRUE(syn_led_is_on(&leds[1]));

    mock_tick_advance(50);
    syn_led_service(leds, 2);
    TEST_ASSERT_TRUE(syn_led_is_on(&leds[0]));
    TEST_ASSERT_FALSE(syn_led_is_on(&leds[1]));

    mock_tick_advance(50);
    syn_led_service(leds, 2);
    TEST_ASSERT_FALSE(syn_led_is_on(&leds[0]));
    TEST_ASSERT_TRUE(syn_led_is_on(&leds[1]));
}

static void test_led_pattern(void)
{
    mock_tick_ms = 0;
    SYN_LED led;
    syn_led_init(&led, 1, SYN_LED_ACTIVE_HIGH);

    /* 1. Empty and NULL pattern handling */
    syn_led_pattern(&led, NULL, 10);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));
    TEST_ASSERT_EQUAL(SYN_LED_MODE_OFF, led.mode);

    syn_led_pattern(&led, "", 10);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));
    TEST_ASSERT_EQUAL(SYN_LED_MODE_OFF, led.mode);

    /* 2. Play pattern ".- |?" with unit = 10ms */
    syn_led_pattern(&led, ".- |?", 10);
    TEST_ASSERT_EQUAL(SYN_LED_MODE_PATTERN, led.mode);
    TEST_ASSERT_FALSE(syn_led_is_on(&led));

    /* '.' - Short on */
    syn_led_update(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    mock_tick_advance(5);
    syn_led_update(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    mock_tick_advance(5);
    syn_led_update(&led);
    TEST_ASSERT_FALSE(syn_led_is_on(&led)); /* Turned off, index -> 1 */

    /* '-' - Long on */
    syn_led_update(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    mock_tick_advance(25);
    syn_led_update(&led);
    TEST_ASSERT_TRUE(syn_led_is_on(&led));

    mock_tick_advance(5);
    syn_led_update(&led);
    TEST_ASSERT_FALSE(syn_led_is_on(&led)); /* Turned off, index -> 2 */

    /* ' ' - Short pause */
    mock_tick_advance(5);
    syn_led_update(&led);
    TEST_ASSERT_EQUAL(2, led.pattern_idx);

    mock_tick_advance(5);
    syn_led_update(&led); /* elapsed >= unit, index -> 3 */
    TEST_ASSERT_EQUAL(3, led.pattern_idx);

    /* '|' - Long pause */
    mock_tick_advance(25);
    syn_led_update(&led);
    TEST_ASSERT_EQUAL(3, led.pattern_idx);

    mock_tick_advance(5);
    syn_led_update(&led); /* elapsed >= 3*unit, index -> 4 */
    TEST_ASSERT_EQUAL(4, led.pattern_idx);

    /* '?' - Unknown char (should skip) */
    syn_led_update(&led); /* skipped, index -> 5 */
    TEST_ASSERT_EQUAL(5, led.pattern_idx);

    /* '\0' - Loop back to start */
    syn_led_update(&led); /* looped, index -> 0 */
    TEST_ASSERT_EQUAL(0, led.pattern_idx);
}

void run_led_tests(void)
{
    RUN_TEST(test_led_basic);
    RUN_TEST(test_led_active_low);
    RUN_TEST(test_led_service);
    RUN_TEST(test_led_pattern);
}

