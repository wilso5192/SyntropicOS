/**
 * @file test_encoder.c
 * @brief Unity tests for syn_encoder.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/input/syn_encoder.h"

static void test_encoder(void)
{

    /* Init with both pins low */
    mock_gpio_states[10] = 0;
    mock_gpio_states[11] = 0;

    SYN_Encoder enc;
    syn_encoder_init(&enc, 10, 11);
    TEST_ASSERT_EQUAL_INT(0, syn_encoder_position(&enc));

    /* Simulate CW rotation: 00 → 10 → 11 → 01 → 00 */
    mock_gpio_states[10] = 1; mock_gpio_states[11] = 0;  /* 10 */
    syn_encoder_update(&enc);

    mock_gpio_states[10] = 1; mock_gpio_states[11] = 1;  /* 11 */
    syn_encoder_update(&enc);

    mock_gpio_states[10] = 0; mock_gpio_states[11] = 1;  /* 01 */
    syn_encoder_update(&enc);

    mock_gpio_states[10] = 0; mock_gpio_states[11] = 0;  /* 00 */
    syn_encoder_update(&enc);

    int32_t delta = syn_encoder_get_delta(&enc);
    TEST_ASSERT_EQUAL_INT(4, delta);
    TEST_ASSERT_EQUAL_INT(4, syn_encoder_position(&enc));

    /* Simulate CCW: 00 → 01 → 11 → 10 → 00 */
    mock_gpio_states[10] = 0; mock_gpio_states[11] = 1;  /* 01 */
    syn_encoder_update(&enc);

    mock_gpio_states[10] = 1; mock_gpio_states[11] = 1;  /* 11 */
    syn_encoder_update(&enc);

    mock_gpio_states[10] = 1; mock_gpio_states[11] = 0;  /* 10 */
    syn_encoder_update(&enc);

    mock_gpio_states[10] = 0; mock_gpio_states[11] = 0;  /* 00 */
    syn_encoder_update(&enc);

    delta = syn_encoder_get_delta(&enc);
    TEST_ASSERT_EQUAL_INT(-4, delta);
    TEST_ASSERT_EQUAL_INT(0, syn_encoder_position(&enc));

    /* Delta auto-resets */
    TEST_ASSERT_EQUAL_INT(0, syn_encoder_get_delta(&enc));

    /* Set position */
    syn_encoder_set_position(&enc, 100);
    TEST_ASSERT_EQUAL_INT(100, syn_encoder_position(&enc));

    /* Steps-per-detent */
    syn_encoder_set_steps_per_detent(&enc, 4);
    syn_encoder_set_position(&enc, 0);

    /* 4 CW state changes = 1 detent */
    mock_gpio_states[10] = 1; mock_gpio_states[11] = 0;
    syn_encoder_update(&enc);
    mock_gpio_states[10] = 1; mock_gpio_states[11] = 1;
    syn_encoder_update(&enc);
    mock_gpio_states[10] = 0; mock_gpio_states[11] = 1;
    syn_encoder_update(&enc);
    mock_gpio_states[10] = 0; mock_gpio_states[11] = 0;
    syn_encoder_update(&enc);

    TEST_ASSERT_EQUAL_INT(1, syn_encoder_position(&enc));
}

/** CCW detent with steps_per_detent=4 — exercises lines 88-90 */
static void test_encoder_ccw_detent(void)
{
    mock_gpio_states[10] = 0;
    mock_gpio_states[11] = 0;
    SYN_Encoder enc;
    syn_encoder_init(&enc, 10, 11);
    syn_encoder_set_steps_per_detent(&enc, 4);

    /* 4 CCW state changes = -1 detent */
    mock_gpio_states[10] = 0; mock_gpio_states[11] = 1;
    syn_encoder_update(&enc);
    mock_gpio_states[10] = 1; mock_gpio_states[11] = 1;
    syn_encoder_update(&enc);
    mock_gpio_states[10] = 1; mock_gpio_states[11] = 0;
    syn_encoder_update(&enc);
    mock_gpio_states[10] = 0; mock_gpio_states[11] = 0;
    syn_encoder_update(&enc);

    TEST_ASSERT_EQUAL_INT(-1, syn_encoder_position(&enc));
    TEST_ASSERT_EQUAL_INT(-1, syn_encoder_get_delta(&enc));
}

/** syn_encoder_set_stats + stats push — exercises lines 103, 109-113 */
static void test_encoder_stats(void)
{
    mock_gpio_states[10] = 0;
    mock_gpio_states[11] = 0;
    SYN_Encoder enc;
    syn_encoder_init(&enc, 10, 11);

    /* Attach a signal for stats */
    static int32_t sig_buf[16];
    static SYN_Signal sig;
    syn_signal_init(&sig, sig_buf, 16);
    syn_encoder_set_stats(&enc, &sig);

    /* One CW step — stats push happens on delta flush (get_delta) */
    mock_gpio_states[10] = 1; mock_gpio_states[11] = 0;
    syn_encoder_update(&enc);
    int32_t delta = syn_encoder_get_delta(&enc);
    TEST_ASSERT_EQUAL_INT(1, delta);
    /* Stats should have one sample pushed */
    TEST_ASSERT_TRUE(syn_signal_count(&sig) >= 1);
}

void run_encoder_tests(void)
{
    RUN_TEST(test_encoder);
    RUN_TEST(test_encoder_ccw_detent);
    RUN_TEST(test_encoder_stats);
}
