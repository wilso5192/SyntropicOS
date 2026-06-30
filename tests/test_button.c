/**
 * @file test_button.c
 * @brief Unity tests for syn_button — full coverage.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/input/syn_button.h"

static int btn_press_count   = 0;
static int btn_release_count = 0;
static int btn_long_count    = 0;
static int btn_repeat_count  = 0;

static void btn_on_press(SYN_Button *b, void *ctx)
    { (void)b; (void)ctx; btn_press_count++; }
static void btn_on_release(SYN_Button *b, void *ctx)
    { (void)b; (void)ctx; btn_release_count++; }
static void btn_on_long(SYN_Button *b, void *ctx)
    { (void)b; (void)ctx; btn_long_count++; }
static void btn_on_repeat(SYN_Button *b, void *ctx)
    { (void)b; (void)ctx; btn_repeat_count++; }

static void reset_counts(void)
{
    btn_press_count = btn_release_count = btn_long_count = btn_repeat_count = 0;
}

/* ── Original test — ACTIVE_HIGH, preserved ──────────────────────────────── */

static void test_button(void)
{
    mock_tick_ms = 0;
    reset_counts();

    /* Pin 0 — we control it via gpio_states (from mock GPIO port) */
    mock_gpio_states[0] = 0; /* not pressed */

    SYN_Button btn;
    syn_button_init(&btn, 0, SYN_BUTTON_ACTIVE_HIGH, 50);
    syn_button_on_press(&btn, btn_on_press, NULL);
    syn_button_on_release(&btn, btn_on_release, NULL);
    syn_button_on_long_press(&btn, btn_on_long, 500, NULL);

    /* No press — should stay idle */
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(0, btn_press_count);
    TEST_ASSERT_FALSE(syn_button_is_pressed(&btn));

    /* Press the button */
    mock_gpio_states[0] = 1;
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(0, btn_press_count);

    /* Advance past debounce window */
    mock_tick_advance(60);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_press_count);
    TEST_ASSERT_TRUE(syn_button_is_pressed(&btn));

    /* Hold for long press */
    mock_tick_advance(500);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_long_count);

    /* Release */
    mock_gpio_states[0] = 0;
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_release_count);
    TEST_ASSERT_FALSE(syn_button_is_pressed(&btn));

    /* Bounce rejection: press then release before debounce */
    btn_press_count = 0;
    mock_gpio_states[0] = 1;
    mock_tick_advance(10);
    syn_button_update(&btn);
    mock_gpio_states[0] = 0; /* bounce off */
    mock_tick_advance(10);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(0, btn_press_count);

    /* Event polling */
    btn.events = 0;
    mock_gpio_states[0] = 1;
    syn_button_update(&btn);
    mock_tick_advance(60);
    syn_button_update(&btn);
    uint8_t evts = syn_button_poll_events(&btn);
    TEST_ASSERT_TRUE(evts & SYN_BUTTON_EVT_PRESS);
    TEST_ASSERT_EQUAL_INT(0, syn_button_poll_events(&btn));
}

/* ── Test: ACTIVE_LOW polarity ───────────────────────────────────────────── */

static void test_button_active_low(void)
{
    mock_tick_ms = 0;
    reset_counts();

    /* Pin 1 — ACTIVE_LOW: GPIO_LOW = pressed */
    mock_gpio_states[1] = SYN_GPIO_HIGH; /* not pressed */

    SYN_Button btn;
    syn_button_init(&btn, 1, SYN_BUTTON_ACTIVE_LOW, 50);
    syn_button_on_press(&btn, btn_on_press, NULL);
    syn_button_on_release(&btn, btn_on_release, NULL);

    /* Not pressed — no event */
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(0, btn_press_count);

    /* Press = drive LOW */
    mock_gpio_states[1] = SYN_GPIO_LOW;
    syn_button_update(&btn);

    mock_tick_advance(60);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_press_count);
    TEST_ASSERT_TRUE(syn_button_is_pressed(&btn));

    /* Release = drive HIGH */
    mock_gpio_states[1] = SYN_GPIO_HIGH;
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_release_count);
}

/* ── Test: repeat in PRESSED state ──────────────────────────────────────── */

static void test_button_repeat_in_pressed(void)
{
    mock_tick_ms = 0;
    reset_counts();

    mock_gpio_states[2] = 0;
    SYN_Button btn;
    syn_button_init(&btn, 2, SYN_BUTTON_ACTIVE_HIGH, 50);
    syn_button_on_press(&btn, btn_on_press, NULL);
    syn_button_on_repeat(&btn, btn_on_repeat, 200, NULL); /* repeat every 200ms */

    /* Press → debounce */
    mock_gpio_states[2] = 1;
    syn_button_update(&btn);
    mock_tick_advance(60);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_press_count);

    /* Hold without long-press threshold — stay in PRESSED state */
    /* Advance past one repeat interval */
    mock_tick_advance(210);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_repeat_count); /* first repeat fired */

    mock_tick_advance(210);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(2, btn_repeat_count); /* second repeat */
}

/* ── Test: repeat in HELD state ─────────────────────────────────────────── */

static void test_button_repeat_in_held(void)
{
    mock_tick_ms = 0;
    reset_counts();

    mock_gpio_states[3] = 0;
    SYN_Button btn;
    syn_button_init(&btn, 3, SYN_BUTTON_ACTIVE_HIGH, 50);
    syn_button_on_press(&btn, btn_on_press, NULL);
    syn_button_on_long_press(&btn, btn_on_long, 300, NULL);
    syn_button_on_repeat(&btn, btn_on_repeat, 150, NULL);

    /* Press → debounce → confirm */
    mock_gpio_states[3] = 1;
    syn_button_update(&btn);
    mock_tick_advance(60);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_press_count);

    /* Hold past long-press threshold → move to HELD state */
    mock_tick_advance(310);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_long_count);

    /* Now in HELD state — advance past repeat interval */
    mock_tick_advance(160);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(1, btn_repeat_count);

    mock_tick_advance(160);
    syn_button_update(&btn);
    TEST_ASSERT_EQUAL_INT(2, btn_repeat_count);

    /* Release from HELD state */
    mock_gpio_states[3] = 0;
    syn_button_update(&btn);
    TEST_ASSERT_FALSE(syn_button_is_pressed(&btn));
}

/* ── Test: syn_button_service (batch update) ─────────────────────────────── */

static void test_button_service(void)
{
    mock_tick_ms = 0;
    reset_counts();

    mock_gpio_states[4] = 0;
    mock_gpio_states[5] = 0;

    SYN_Button btns[2];
    syn_button_init(&btns[0], 4, SYN_BUTTON_ACTIVE_HIGH, 50);
    syn_button_init(&btns[1], 5, SYN_BUTTON_ACTIVE_HIGH, 50);
    syn_button_on_press(&btns[0], btn_on_press, NULL);
    syn_button_on_press(&btns[1], btn_on_press, NULL);

    /* Press both buttons */
    mock_gpio_states[4] = 1;
    mock_gpio_states[5] = 1;
    syn_button_service(btns, 2);

    mock_tick_advance(60);
    syn_button_service(btns, 2);

    /* Both should have fired press events */
    TEST_ASSERT_EQUAL_INT(2, btn_press_count);

    /* service with count=0 — should not crash */
    syn_button_service(btns, 0);
}

void run_button_tests(void)
{
    RUN_TEST(test_button);
    RUN_TEST(test_button_active_low);
    RUN_TEST(test_button_repeat_in_pressed);
    RUN_TEST(test_button_repeat_in_held);
    RUN_TEST(test_button_service);
}
