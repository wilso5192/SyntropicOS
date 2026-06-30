/**
 * @file test_adc.c
 * @brief Unity tests for syn_adc — full coverage (adds filter and stats paths).
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"
#include "syntropic/drivers/syn_adc.h"
#include "syntropic/dsp/syn_filter.h"
#include "syntropic/dsp/syn_signal.h"

static void test_adc(void)
{
    SYN_ADC adc;
    SYN_ADC_Config cfg = {
        .channel = 0,
        .oversample = 4,
        .filter = NULL,
        .filter_type = SYN_ADC_FILTER_NONE,
        .cal_offset = 0,
        .cal_scale = 1,
        .cal_scale_shift = 0,
    };

    mock_adc_value = 2048;
    syn_adc_init(&adc, &cfg);

    int32_t val = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(2048, val);
    TEST_ASSERT_EQUAL_INT(2048, syn_adc_raw(&adc));

    /* mV conversion: 2048/4095 * 3300 ≈ 1649 */
    int32_t mv = syn_adc_read_mv(&adc);
    TEST_ASSERT_TRUE(mv >= 1640 && mv <= 1660);

    /* With calibration offset */
    syn_adc_set_calibration(&adc, 100, 1, 0);
    val = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(2148, val);

    /* With scale */
    syn_adc_set_calibration(&adc, 0, 2048, 10);  /* × 2.0 */
    val = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(4096, val);
}

/** MA filter integration — exercises SYN_ADC_FILTER_MA path */
static void test_adc_filter_ma(void)
{
    static SYN_FilterMA ma_filter;
    syn_filter_ma_init(&ma_filter, 4);

    SYN_ADC adc;
    SYN_ADC_Config cfg = {
        .channel    = 0,
        .oversample = 1,
        .filter     = &ma_filter,
        .filter_type = SYN_ADC_FILTER_MA,
        .cal_offset = 0,
        .cal_scale  = 1,
        .cal_scale_shift = 0,
    };
    mock_adc_value = 100;
    syn_adc_init(&adc, &cfg);

    /* First reading seeds the MA filter */
    int32_t v = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(100, v);

    /* Second reading averages */
    mock_adc_value = 200;
    v = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(150, v);
}

/** EMA filter integration — exercises SYN_ADC_FILTER_EMA path */
static void test_adc_filter_ema(void)
{
    static SYN_FilterEMA ema_filter;
    syn_filter_ema_init(&ema_filter, 128);

    SYN_ADC adc;
    SYN_ADC_Config cfg = {
        .channel    = 0,
        .oversample = 1,
        .filter     = &ema_filter,
        .filter_type = SYN_ADC_FILTER_EMA,
        .cal_offset = 0,
        .cal_scale  = 1,
        .cal_scale_shift = 0,
    };
    mock_adc_value = 100;
    syn_adc_init(&adc, &cfg);

    int32_t v = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(100, v); /* first value seeds EMA */

    mock_adc_value = 200;
    v = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(150, v); /* EMA average */
}

/** Median filter integration — exercises SYN_ADC_FILTER_MEDIAN path */
static void test_adc_filter_median(void)
{
    static SYN_FilterMedian med_filter;
    syn_filter_median_init(&med_filter, 3);

    SYN_ADC adc;
    SYN_ADC_Config cfg = {
        .channel    = 0,
        .oversample = 1,
        .filter     = &med_filter,
        .filter_type = SYN_ADC_FILTER_MEDIAN,
        .cal_offset = 0,
        .cal_scale  = 1,
        .cal_scale_shift = 0,
    };
    mock_adc_value = 50;
    syn_adc_init(&adc, &cfg);

    /* Fill the 3-wide median window */
    syn_adc_read(&adc);
    mock_adc_value = 100;
    syn_adc_read(&adc);
    mock_adc_value = 9999; /* spike */
    int32_t v = syn_adc_read(&adc);
    /* Median of [50, 100, 9999] = 100 */
    TEST_ASSERT_EQUAL_INT(100, v);
}

/** Unknown filter type — exercises default branch (returns value as-is) */
static void test_adc_filter_default(void)
{
    static int dummy_filter = 0;

    SYN_ADC adc;
    SYN_ADC_Config cfg = {
        .channel    = 0,
        .oversample = 1,
        .filter     = &dummy_filter,
        .filter_type = 99, /* unknown type → default branch */
        .cal_offset = 0,
        .cal_scale  = 1,
        .cal_scale_shift = 0,
    };
    mock_adc_value = 512;
    syn_adc_init(&adc, &cfg);

    int32_t v = syn_adc_read(&adc);
    TEST_ASSERT_EQUAL_INT(512, v); /* passthrough */
}

/** Stats window — exercises syn_signal_push path */
static void test_adc_stats(void)
{
    static int32_t samples[16];
    static SYN_Signal sig;
    syn_signal_init(&sig, samples, 16);

    SYN_ADC adc;
    SYN_ADC_Config cfg = {
        .channel    = 0,
        .oversample = 1,
        .filter     = NULL,
        .filter_type = SYN_ADC_FILTER_NONE,
        .cal_offset = 0,
        .cal_scale  = 1,
        .cal_scale_shift = 0,
    };
    mock_adc_value = 1000;
    syn_adc_init(&adc, &cfg);

    /* Attach stats window */
    syn_adc_set_stats(&adc, &sig);

    /* Read 4 values — each should be pushed to signal */
    for (int i = 0; i < 4; i++) {
        mock_adc_value = (uint16_t)(1000 + i * 100);
        syn_adc_read(&adc);
    }

    TEST_ASSERT_EQUAL_INT(4, (int)syn_signal_count(&sig));
    int32_t mean = syn_signal_mean(&sig);
    TEST_ASSERT_TRUE(mean >= 1000 && mean <= 1400);
}

void run_adc_tests(void)
{
    RUN_TEST(test_adc);
    RUN_TEST(test_adc_filter_ma);
    RUN_TEST(test_adc_filter_ema);
    RUN_TEST(test_adc_filter_median);
    RUN_TEST(test_adc_filter_default);
    RUN_TEST(test_adc_stats);
}
