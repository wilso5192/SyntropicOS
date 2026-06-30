/**
 * @file test_fwupdate.c
 * @brief Tests for firmware image headers, streaming updater, and A/B boot.
 */

#include "unity/unity.h"
#include "syntropic/system/syn_fwimage.h"
#include "syntropic/system/syn_fwupdate.h"
#include "syntropic/system/syn_fwboot.h"
#include "syntropic/util/syn_crc.h"
#include "syntropic/port/syn_port_flash.h"
#include "mocks/mock_port.h"

#include <string.h>

/* Use the mock flash for all operations.
 * Layout: slot A at 0x0000, slot B at 0x0800 (within mock_flash[4096]).
 * Each slot is 2048 bytes (2 sectors of 1024). */
#define SLOT_A_ADDR   0x0000u
#define SLOT_B_ADDR   0x0800u
#define SLOT_SIZE     (2048u - (uint32_t)sizeof(SYN_FwImageHeader))

/* ── Image header tests ─────────────────────────────────────────────────── */

void test_fwimage_seal_and_validate(void)
{
    SYN_FwImageHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic        = SYN_FW_MAGIC;
    hdr.version_code = 0x00010200;  /* v1.2.0 */
    hdr.image_size   = 512;
    hdr.image_crc    = 0xDEADBEEF;
    hdr.state        = SYN_FW_STATE_CONFIRMED;

    syn_fwimage_seal_header(&hdr);

    TEST_ASSERT_TRUE(syn_fwimage_header_valid(&hdr));
    TEST_ASSERT_TRUE(syn_fwimage_is_bootable(&hdr));
}

void test_fwimage_bad_magic(void)
{
    SYN_FwImageHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = 0x12345678; /* wrong */
    syn_fwimage_seal_header(&hdr);

    TEST_ASSERT_FALSE(syn_fwimage_header_valid(&hdr));
}

void test_fwimage_corrupted_crc(void)
{
    SYN_FwImageHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic        = SYN_FW_MAGIC;
    hdr.version_code = 0x00010000;
    hdr.state        = SYN_FW_STATE_CONFIRMED;
    syn_fwimage_seal_header(&hdr);

    /* Corrupt the version */
    hdr.version_code = 0x00020000;
    TEST_ASSERT_FALSE(syn_fwimage_header_valid(&hdr));
}

void test_fwimage_bootable_states(void)
{
    SYN_FwImageHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SYN_FW_MAGIC;

    /* CONFIRMED = bootable */
    hdr.state = SYN_FW_STATE_CONFIRMED;
    syn_fwimage_seal_header(&hdr);
    TEST_ASSERT_TRUE(syn_fwimage_is_bootable(&hdr));

    /* NEW = bootable */
    hdr.state = SYN_FW_STATE_NEW;
    syn_fwimage_seal_header(&hdr);
    TEST_ASSERT_TRUE(syn_fwimage_is_bootable(&hdr));

    /* TESTING = bootable */
    hdr.state = SYN_FW_STATE_TESTING;
    syn_fwimage_seal_header(&hdr);
    TEST_ASSERT_TRUE(syn_fwimage_is_bootable(&hdr));

    /* WRITING = not bootable */
    hdr.state = SYN_FW_STATE_WRITING;
    syn_fwimage_seal_header(&hdr);
    TEST_ASSERT_FALSE(syn_fwimage_is_bootable(&hdr));

    /* INVALID = not bootable */
    hdr.state = SYN_FW_STATE_INVALID;
    syn_fwimage_seal_header(&hdr);
    TEST_ASSERT_FALSE(syn_fwimage_is_bootable(&hdr));
}

/* ── Firmware update tests ──────────────────────────────────────────────── */

void test_fwupdate_basic(void)
{
    mock_port_reset();

    static uint8_t page_buf[64];
    SYN_FwUpdate upd;

    /* Create a small firmware image */
    uint8_t firmware[128];
    for (int i = 0; i < 128; i++) firmware[i] = (uint8_t)i;

    uint32_t expected_crc = syn_crc32(firmware, sizeof(firmware));

    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE,
                                        page_buf, sizeof(page_buf));
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_TRUE(syn_fwupdate_active(&upd));

    /* Write in chunks */
    st = syn_fwupdate_write(&upd, firmware, 50);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    st = syn_fwupdate_write(&upd, firmware + 50, 78);
    TEST_ASSERT_EQUAL(SYN_OK, st);

    TEST_ASSERT_EQUAL(128, syn_fwupdate_progress(&upd) + upd.page_buf_used);

    st = syn_fwupdate_finish(&upd, expected_crc, NULL, 0x00010200);
    TEST_ASSERT_EQUAL(SYN_OK, st);
    TEST_ASSERT_FALSE(syn_fwupdate_active(&upd));

    /* Verify the header in flash */
    SYN_FwImageHeader hdr;
    syn_port_flash_read(SLOT_A_ADDR, &hdr, sizeof(hdr));
    TEST_ASSERT_TRUE(syn_fwimage_header_valid(&hdr));
    TEST_ASSERT_EQUAL(SYN_FW_STATE_NEW, hdr.state);
    TEST_ASSERT_EQUAL_HEX32(0x00010200, hdr.version_code);
    TEST_ASSERT_EQUAL(128, hdr.image_size);
    TEST_ASSERT_EQUAL_HEX32(expected_crc, hdr.image_crc);
}

void test_fwupdate_crc_mismatch(void)
{
    mock_port_reset();

    static uint8_t page_buf[64];
    SYN_FwUpdate upd;

    uint8_t firmware[32];
    memset(firmware, 0xAA, sizeof(firmware));

    syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE,
                       page_buf, sizeof(page_buf));
    syn_fwupdate_write(&upd, firmware, sizeof(firmware));

    /* Provide wrong CRC */
    SYN_Status st = syn_fwupdate_finish(&upd, 0xBADBAD00, NULL, 0x00010000);
    TEST_ASSERT_EQUAL(SYN_ERROR, st);

    /* Slot should be marked INVALID */
    SYN_FwImageHeader hdr;
    syn_port_flash_read(SLOT_A_ADDR, &hdr, sizeof(hdr));
    TEST_ASSERT_EQUAL(SYN_FW_STATE_INVALID, hdr.state);
}

void test_fwupdate_abort(void)
{
    mock_port_reset();

    static uint8_t page_buf[64];
    SYN_FwUpdate upd;

    syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE,
                       page_buf, sizeof(page_buf));

    uint8_t chunk[16] = {0};
    syn_fwupdate_write(&upd, chunk, sizeof(chunk));

    syn_fwupdate_abort(&upd);

    TEST_ASSERT_FALSE(syn_fwupdate_active(&upd));

    SYN_FwImageHeader hdr;
    syn_port_flash_read(SLOT_A_ADDR, &hdr, sizeof(hdr));
    TEST_ASSERT_EQUAL(SYN_FW_STATE_INVALID, hdr.state);
}

/* ── A/B Boot tests ─────────────────────────────────────────────────────── */

/** Helper: write a valid image header to flash at the given address. */
static void write_test_header(uint32_t addr, uint8_t state, uint32_t version)
{
    SYN_FwImageHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic        = SYN_FW_MAGIC;
    hdr.version_code = version;
    hdr.image_size   = 64;
    hdr.image_crc    = 0xDEADBEEF;
    hdr.state        = state;
    syn_fwimage_seal_header(&hdr);

    syn_port_flash_erase(addr);
    syn_port_flash_write(addr, &hdr, sizeof(hdr));
}

void test_fwboot_select_confirmed(void)
{
    mock_port_reset();

    /* Slot A: confirmed v1.0.0, Slot B: empty */
    write_test_header(SLOT_A_ADDR, SYN_FW_STATE_CONFIRMED, 0x00010000);

    SYN_FwBootManager mgr;
    syn_fwboot_init(&mgr, SLOT_A_ADDR, SLOT_B_ADDR);

    uint8_t slot = syn_fwboot_select(&mgr, false);
    TEST_ASSERT_EQUAL(SYN_FW_SLOT_A, slot);
}

void test_fwboot_select_new_over_confirmed(void)
{
    mock_port_reset();

    /* Slot A: confirmed v1.0.0, Slot B: NEW v2.0.0 */
    write_test_header(SLOT_A_ADDR, SYN_FW_STATE_CONFIRMED, 0x00010000);
    write_test_header(SLOT_B_ADDR, SYN_FW_STATE_NEW, 0x00020000);

    SYN_FwBootManager mgr;
    syn_fwboot_init(&mgr, SLOT_A_ADDR, SLOT_B_ADDR);

    uint8_t slot = syn_fwboot_select(&mgr, false);
    TEST_ASSERT_EQUAL(SYN_FW_SLOT_B, slot);

    /* After select, NEW should be promoted to TESTING */
    SYN_FwImageHeader hdr;
    syn_port_flash_read(SLOT_B_ADDR, &hdr, sizeof(hdr));
    TEST_ASSERT_EQUAL(SYN_FW_STATE_TESTING, hdr.state);
}

void test_fwboot_rollback(void)
{
    mock_port_reset();

    /* Slot A: CONFIRMED v1.0.0, Slot B: TESTING v2.0.0 (failed) */
    write_test_header(SLOT_A_ADDR, SYN_FW_STATE_CONFIRMED, 0x00010000);
    write_test_header(SLOT_B_ADDR, SYN_FW_STATE_TESTING, 0x00020000);

    SYN_FwBootManager mgr;
    syn_fwboot_init(&mgr, SLOT_A_ADDR, SLOT_B_ADDR);

    /* Rollback: TESTING slot should be invalidated */
    uint8_t slot = syn_fwboot_select(&mgr, true);
    TEST_ASSERT_EQUAL(SYN_FW_SLOT_A, slot);

    /* Slot B should now be INVALID */
    SYN_FwImageHeader hdr;
    syn_port_flash_read(SLOT_B_ADDR, &hdr, sizeof(hdr));
    TEST_ASSERT_EQUAL(SYN_FW_STATE_INVALID, hdr.state);
}

void test_fwboot_confirm(void)
{
    mock_port_reset();

    /* Slot A: TESTING v2.0.0 */
    write_test_header(SLOT_A_ADDR, SYN_FW_STATE_TESTING, 0x00020000);

    SYN_FwBootManager mgr;
    syn_fwboot_init(&mgr, SLOT_A_ADDR, SLOT_B_ADDR);
    syn_fwboot_select(&mgr, false);

    SYN_Status st = syn_fwboot_confirm(&mgr);
    TEST_ASSERT_EQUAL(SYN_OK, st);

    SYN_FwImageHeader hdr;
    syn_port_flash_read(SLOT_A_ADDR, &hdr, sizeof(hdr));
    TEST_ASSERT_EQUAL(SYN_FW_STATE_CONFIRMED, hdr.state);
}

void test_fwboot_higher_version_wins(void)
{
    mock_port_reset();

    /* Both confirmed, slot B has higher version */
    write_test_header(SLOT_A_ADDR, SYN_FW_STATE_CONFIRMED, 0x00010000);
    write_test_header(SLOT_B_ADDR, SYN_FW_STATE_CONFIRMED, 0x00020000);

    SYN_FwBootManager mgr;
    syn_fwboot_init(&mgr, SLOT_A_ADDR, SLOT_B_ADDR);

    uint8_t slot = syn_fwboot_select(&mgr, false);
    TEST_ASSERT_EQUAL(SYN_FW_SLOT_B, slot);
}

/* ── Runner ─────────────────────────────────────────────────────────────── */

/** flash_erase fails in begin — exercises lines 77-79 */
static void test_fwupdate_erase_fail(void)
{
    mock_port_reset();
    static uint8_t pbuf[64];
    SYN_FwUpdate upd;

    /* Make erase fail at SLOT_A_ADDR */
    mock_flash_fail_at = SLOT_A_ADDR;
    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE, pbuf, sizeof(pbuf));
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
    TEST_ASSERT_TRUE(upd.error);
    TEST_ASSERT_FALSE(upd.active);
}

/** flash_write header fails in begin — exercises lines 91-92 */
static void test_fwupdate_write_header_fail(void)
{
    mock_port_reset();
    static uint8_t pbuf[64];
    SYN_FwUpdate upd;

    /* Erase succeeds, but the very next write (header) fails */
    mock_flash_write_fail_next = true;
    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE, pbuf, sizeof(pbuf));
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
    TEST_ASSERT_TRUE(upd.error);
    TEST_ASSERT_FALSE(upd.active);
}

/** flush_page write fails during write — exercises lines 128-129 */
static void test_fwupdate_write_flush_fail(void)
{
    mock_port_reset();
    static uint8_t pbuf[4]; /* small page buffer so it fills quickly */
    SYN_FwUpdate upd;

    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE, pbuf, sizeof(pbuf));
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Fill exactly the page buffer to trigger a flush — then fail the write */
    mock_flash_write_fail_next = true;
    uint8_t data[4] = { 0x01, 0x02, 0x03, 0x04 };
    st = syn_fwupdate_write(&upd, data, sizeof(data));
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
    TEST_ASSERT_TRUE(upd.error);
}

/** flush_page write fails in finish — exercises lines 147-148 */
static void test_fwupdate_finish_flush_fail(void)
{
    mock_port_reset();
    static uint8_t pbuf[64];
    SYN_FwUpdate upd;

    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE, pbuf, sizeof(pbuf));
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Write some data (doesn't fill page buffer yet) */
    uint8_t data[5] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    st = syn_fwupdate_write(&upd, data, sizeof(data));
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Fail the flush write that happens inside finish() */
    mock_flash_write_fail_next = true;
    uint32_t crc = syn_crc32_final(syn_crc32_update(SYN_CRC32_INIT, data, sizeof(data)));
    st = syn_fwupdate_finish(&upd, crc, NULL, 1);
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
    TEST_ASSERT_TRUE(upd.error);
}

/** flash_erase fails in finish — exercises lines 178-179 */
static void test_fwupdate_finish_erase_fail(void)
{
    mock_port_reset();
    static uint8_t pbuf[64];
    SYN_FwUpdate upd;

    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE, pbuf, sizeof(pbuf));
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Write a byte, compute CRC */
    uint8_t data[1] = { 0xAB };
    st = syn_fwupdate_write(&upd, data, sizeof(data));
    TEST_ASSERT_EQUAL(SYN_OK, st);
    uint32_t crc = syn_crc32_final(syn_crc32_update(SYN_CRC32_INIT, data, sizeof(data)));

    /* Fail the erase that happens at the start of finish() header-rewrite */
    mock_flash_fail_at = SLOT_A_ADDR;
    st = syn_fwupdate_finish(&upd, crc, NULL, 1);
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
    TEST_ASSERT_TRUE(upd.error);
}

/** write exceeds max_size — exercises lines 106-107 */
static void test_fwupdate_write_overflow(void)
{
    mock_port_reset();
    static uint8_t pbuf[64];
    SYN_FwUpdate upd;

    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, 32, pbuf, sizeof(pbuf));
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Write more than max_size (32) */
    uint8_t data[100];
    memset(data, 0xAB, sizeof(data));
    st = syn_fwupdate_write(&upd, data, sizeof(data));
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
    TEST_ASSERT_TRUE(upd.error);
}

/** syn_fwboot_refresh — exercises lines 149-155 */
static void test_fwboot_refresh(void)
{
    SYN_FwBootManager mgr;
    syn_fwboot_init(&mgr, SLOT_A_ADDR, SLOT_B_ADDR);
    SYN_Status st = syn_fwboot_refresh(&mgr);
    TEST_ASSERT_EQUAL(SYN_OK, st);
}
/** Write: sector-boundary erase fails — exercises lines 39-40 */
static void test_fwupdate_sector_erase_fail(void)
{
    mock_port_reset();
    static uint8_t pbuf[64];
    SYN_FwUpdate upd;

    SYN_Status st = syn_fwupdate_begin(&upd, SLOT_A_ADDR, SLOT_SIZE,
                                        pbuf, sizeof(pbuf));
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Write enough to fill first sector (1024 - header size).
     * data_addr = sizeof(SYN_FwImageHeader), so we need to write
     * 1024 - sizeof(SYN_FwImageHeader) bytes to reach sector 1024 boundary */
    size_t fill = 1024 - (size_t)sizeof(SYN_FwImageHeader);
    uint8_t chunk[64];
    memset(chunk, 0xAA, sizeof(chunk));

    while (fill > 0) {
        size_t n = fill > sizeof(chunk) ? sizeof(chunk) : fill;
        st = syn_fwupdate_write(&upd, chunk, n);
        if (st != SYN_OK) break;
        fill -= n;
    }
    TEST_ASSERT_EQUAL(SYN_OK, st);

    /* Now the next write crosses into sector at addr 1024.
     * Make erase fail at that address. */
    mock_flash_fail_at = 1024;
    st = syn_fwupdate_write(&upd, chunk, sizeof(chunk));
    TEST_ASSERT_EQUAL(SYN_ERROR, st);
}

void run_fwupdate_tests(void)
{
    /* Image header */
    RUN_TEST(test_fwimage_seal_and_validate);
    RUN_TEST(test_fwimage_bad_magic);
    RUN_TEST(test_fwimage_corrupted_crc);
    RUN_TEST(test_fwimage_bootable_states);

    /* Streaming updater */
    RUN_TEST(test_fwupdate_basic);
    RUN_TEST(test_fwupdate_crc_mismatch);
    RUN_TEST(test_fwupdate_abort);

    /* A/B Boot */
    RUN_TEST(test_fwboot_select_confirmed);
    RUN_TEST(test_fwboot_select_new_over_confirmed);
    RUN_TEST(test_fwboot_rollback);
    RUN_TEST(test_fwboot_confirm);
    RUN_TEST(test_fwboot_higher_version_wins);
    RUN_TEST(test_fwboot_refresh);
    RUN_TEST(test_fwupdate_erase_fail);
    RUN_TEST(test_fwupdate_write_header_fail);
    RUN_TEST(test_fwupdate_write_overflow);
    RUN_TEST(test_fwupdate_write_flush_fail);
    RUN_TEST(test_fwupdate_finish_flush_fail);
    RUN_TEST(test_fwupdate_finish_erase_fail);
    RUN_TEST(test_fwupdate_sector_erase_fail);
}
