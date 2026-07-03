/**
 * @file test_settings.c
 * @brief Unit tests for syn_settings — persistent settings with change detection.
 *
 * Tests init (blank flash / pre-existing data), save, change detection,
 * reset to defaults, callback, reload, and no-op save optimization.
 */

#include "unity/unity.h"
#include "mocks/mock_port.h"
#include "syntropic/storage/syn_settings.h"
#include <string.h>

/* ── Test data ─────────────────────────────────────────────────────────── */

typedef struct {
    int32_t velocity;
    int32_t accel;
    uint8_t mode;
    uint8_t _pad[3];  /* Align to 4 bytes */
} TestSettings;

static const TestSettings defaults = { .velocity = 500, .accel = 200, .mode = 1 };

/* Flash layout: use base address 0, 4 sectors */
#define FLASH_BASE    0
#define SECTOR_COUNT  4

/* ── Callback tracking ─────────────────────────────────────────────────── */

static int      change_cb_count;
static void    *change_cb_data;
static void    *change_cb_ctx;

static void on_change(void *data, void *ctx)
{
    change_cb_count++;
    change_cb_data = data;
    change_cb_ctx  = ctx;
}

/* ── Tests ──────────────────────────────────────────────────────────────── */

static void test_settings_init_blank_flash(void)
{
    /* Flash is blank (all 0xFF from mock_port_reset) */
    TestSettings settings;
    SYN_Settings store;

    SYN_Status st = syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                                       &settings, sizeof(settings), &defaults);
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Should have loaded defaults */
    TEST_ASSERT_EQUAL_INT32(500, settings.velocity);
    TEST_ASSERT_EQUAL_INT32(200, settings.accel);
    TEST_ASSERT_EQUAL_UINT8(1, settings.mode);
}

static void test_settings_save_and_reload(void)
{
    TestSettings settings;
    SYN_Settings store;

    syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                      &settings, sizeof(settings), &defaults);

    /* Modify and save */
    settings.velocity = 800;
    SYN_Status st = syn_settings_save(&store);
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Re-init from same flash — should load saved values */
    TestSettings settings2;
    SYN_Settings store2;

    st = syn_settings_init(&store2, FLASH_BASE, SECTOR_COUNT,
                           &settings2, sizeof(settings2), &defaults);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_EQUAL_INT32(800, settings2.velocity);
    TEST_ASSERT_EQUAL_INT32(200, settings2.accel);  /* unchanged */
}

static void test_settings_change_detection(void)
{
    TestSettings settings;
    SYN_Settings store;

    syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                      &settings, sizeof(settings), &defaults);

    /* Immediately after init, no changes */
    TEST_ASSERT_FALSE(syn_settings_changed(&store));

    /* Modify data in-place */
    settings.accel = 999;
    TEST_ASSERT_TRUE(syn_settings_changed(&store));

    /* Save — should clear the changed flag */
    syn_settings_save(&store);
    TEST_ASSERT_FALSE(syn_settings_changed(&store));
}

static void test_settings_save_noop_if_unchanged(void)
{
    TestSettings settings;
    SYN_Settings store;

    syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                      &settings, sizeof(settings), &defaults);

    uint16_t crc_before = syn_settings_checksum(&store);

    /* Save without changing anything */
    SYN_Status st = syn_settings_save(&store);
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Checksum should not have changed */
    TEST_ASSERT_EQUAL_UINT16(crc_before, syn_settings_checksum(&store));
}

static void test_settings_reset_to_defaults(void)
{
    TestSettings settings;
    SYN_Settings store;

    syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                      &settings, sizeof(settings), &defaults);

    /* Modify */
    settings.velocity = 9999;
    settings.accel = 0;
    settings.mode = 7;
    syn_settings_save(&store);

    /* Reset */
    SYN_Status st = syn_settings_reset(&store);
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Should be back to defaults */
    TEST_ASSERT_EQUAL_INT32(500, settings.velocity);
    TEST_ASSERT_EQUAL_INT32(200, settings.accel);
    TEST_ASSERT_EQUAL_UINT8(1, settings.mode);

    /* Reload from flash to verify persisted */
    TestSettings settings2;
    SYN_Settings store2;
    syn_settings_init(&store2, FLASH_BASE, SECTOR_COUNT,
                      &settings2, sizeof(settings2), &defaults);
    TEST_ASSERT_EQUAL_INT32(500, settings2.velocity);
}

static void test_settings_change_callback(void)
{
    TestSettings settings;
    SYN_Settings store;

    syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                      &settings, sizeof(settings), &defaults);

    change_cb_count = 0;
    change_cb_data  = NULL;
    change_cb_ctx   = NULL;

    int ctx_value = 42;
    syn_settings_on_change(&store, on_change, &ctx_value);

    /* Save without changes — callback should NOT fire */
    syn_settings_save(&store);
    TEST_ASSERT_EQUAL_INT(0, change_cb_count);

    /* Save with changes — callback should fire */
    settings.mode = 5;
    syn_settings_save(&store);
    TEST_ASSERT_EQUAL_INT(1, change_cb_count);
    TEST_ASSERT_EQUAL_PTR(&settings, change_cb_data);
    TEST_ASSERT_EQUAL_PTR(&ctx_value, change_cb_ctx);
}

static void test_settings_reload_discards_changes(void)
{
    TestSettings settings;
    SYN_Settings store;

    syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                      &settings, sizeof(settings), &defaults);

    /* Save known state */
    settings.velocity = 600;
    syn_settings_save(&store);

    /* Modify without saving */
    settings.velocity = 12345;
    TEST_ASSERT_TRUE(syn_settings_changed(&store));

    /* Reload from flash — should discard */
    SYN_Status st = syn_settings_reload(&store);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_EQUAL_INT32(600, settings.velocity);
    TEST_ASSERT_FALSE(syn_settings_changed(&store));
}

static void test_settings_checksum_changes_on_save(void)
{
    TestSettings settings;
    SYN_Settings store;

    syn_settings_init(&store, FLASH_BASE, SECTOR_COUNT,
                      &settings, sizeof(settings), &defaults);

    uint16_t crc1 = syn_settings_checksum(&store);

    settings.velocity = 777;
    syn_settings_save(&store);

    uint16_t crc2 = syn_settings_checksum(&store);

    /* Checksums should differ after data change */
    TEST_ASSERT_NOT_EQUAL(crc1, crc2);
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

void run_settings_tests(void)
{
    RUN_TEST(test_settings_init_blank_flash);
    RUN_TEST(test_settings_save_and_reload);
    RUN_TEST(test_settings_change_detection);
    RUN_TEST(test_settings_save_noop_if_unchanged);
    RUN_TEST(test_settings_reset_to_defaults);
    RUN_TEST(test_settings_change_callback);
    RUN_TEST(test_settings_reload_discards_changes);
    RUN_TEST(test_settings_checksum_changes_on_save);
}
