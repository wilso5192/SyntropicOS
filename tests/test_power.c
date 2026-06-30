/**
 * @file test_power.c
 * @brief Unity tests for syn_power — full coverage (adds errlog/brownout log).
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/syntropic.h"

#include "syntropic/system/syn_power.h"
#include "syntropic/system/syn_errlog.h"

static int power_bo_count = 0;
static int power_re_count = 0;
static void pwr_bo_cb(SYN_Power *p, void *c) { (void)p; (void)c; power_bo_count++; }
static void pwr_re_cb(SYN_Power *p, void *c) { (void)p; (void)c; power_re_count++; }

static void test_power(void)
{
    static SYN_ADC pwr_adc;
    SYN_ADC_Config pwr_adc_cfg = { .channel = 0, .oversample = 1, .cal_scale = 1 };
    mock_adc_value = 4095; /* ~3300mV */
    syn_adc_init(&pwr_adc, &pwr_adc_cfg);
    power_bo_count = 0; power_re_count = 0;
    SYN_Power pwr;
    SYN_Power_Config pcfg = {
        .adc = &pwr_adc, .brownout_mv = 3000, .restore_mv = 3200,
        .on_brownout = pwr_bo_cb, .on_restore = pwr_re_cb,
    };
    syn_power_init(&pwr, &pcfg);
    syn_power_update(&pwr);
    TEST_ASSERT_TRUE(!syn_power_is_brownout(&pwr));
    mock_adc_value = 3600; /* ~2900mV */
    syn_power_update(&pwr);
    TEST_ASSERT_TRUE(syn_power_is_brownout(&pwr));
    TEST_ASSERT_EQUAL_INT(1, power_bo_count);
    syn_power_update(&pwr);
    TEST_ASSERT_EQUAL_INT(1, power_bo_count);
    mock_adc_value = 4095; /* ~3300mV */
    syn_power_update(&pwr);
    TEST_ASSERT_TRUE(!syn_power_is_brownout(&pwr));
    TEST_ASSERT_EQUAL_INT(1, power_re_count);
    SYN_Signal pwr_stats;
    int32_t pwr_samp[8];
    syn_signal_init(&pwr_stats, pwr_samp, 8);
    syn_power_set_stats(&pwr, &pwr_stats);
    syn_power_update(&pwr);
    TEST_ASSERT_TRUE(syn_signal_count(&pwr_stats) == 1);
}

/**
 * syn_power_set_errlog + brownout with errlog — exercises lines 58-59 and 78-82.
 */
static void test_power_errlog(void)
{
    static SYN_ADC pwr_adc;
    SYN_ADC_Config pwr_adc_cfg = { .channel = 0, .oversample = 1, .cal_scale = 1 };
    power_bo_count = 0; power_re_count = 0;

    mock_adc_value = 4095; /* healthy voltage */
    syn_adc_init(&pwr_adc, &pwr_adc_cfg);

    SYN_Power pwr;
    SYN_Power_Config pcfg = {
        .adc = &pwr_adc, .brownout_mv = 3000, .restore_mv = 3200,
        .on_brownout = pwr_bo_cb, .on_restore = pwr_re_cb,
    };
    syn_power_init(&pwr, &pcfg);

    /* Attach errlog */
    static SYN_ErrEntry errlog_buf[8];
    static SYN_ErrLog errlog;
    syn_errlog_init(&errlog, errlog_buf, 8, 1);
    syn_power_set_errlog(&pwr, &errlog);
    TEST_ASSERT_EQUAL_PTR(&errlog, pwr.errlog);

    /* First update — healthy, no errlog entry */
    syn_power_update(&pwr);
    TEST_ASSERT_EQUAL_INT(0, (int)syn_errlog_count(&errlog));

    /* Trigger brownout — errlog should record the event */
    mock_adc_value = 3600; /* low voltage */
    syn_power_update(&pwr);
    TEST_ASSERT_TRUE(syn_power_is_brownout(&pwr));
    TEST_ASSERT_EQUAL_INT(1, power_bo_count);
    /* errlog should have at least one entry */
    TEST_ASSERT_TRUE(syn_errlog_count(&errlog) > 0);
}

void run_power_tests(void)
{
    RUN_TEST(test_power);
    RUN_TEST(test_power_errlog);
}
