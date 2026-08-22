// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"
#include <string.h>

extern "C" {
#include "keyboards/era/common/features/era_tapping.h"
#include "keyboards/era/common/features/era_tapping_via.h"
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/features/era_tapdance_via.h"
#include "keyboards/era/common/split/era_split_eeprom_sync.h"
#include "keyboards/era/common/storage/era_eeprom_config_io.h"
#include "keyboards/era/common/system/era_state_sync.h"
#include "keycode_config.h"
#include "nvm_dynamic_keymap.h"
#include "nvm_eeconfig.h"
#include "nvm_via.h"
#include "rgb_matrix_types.h"
#include "via.h"

led_config_t              g_led_config      = {};
const rgb_matrix_driver_t rgb_matrix_driver = {};
}

static constexpr uint16_t kEraProtectedConfigStart = 176;

static uint8_t  g_last_send[32];
static uint32_t g_send_count;

extern "C" void nvm_eeprom_changed_kb(uint16_t offset, uint16_t length) {
    era_state_sync_note_eeprom_span(offset, length);
}

extern "C" bool process_record_via(uint16_t, keyrecord_t *) {
    return true;
}
extern "C" bool via_eeprom_is_valid(void) {
    return true;
}
extern "C" void via_init(void) {}
extern "C" void eeconfig_init_via(void) {}

extern "C" void raw_hid_send(uint8_t *data, uint8_t length) {
    g_send_count++;
    memset(g_last_send, 0, sizeof(g_last_send));
    memcpy(g_last_send, data, length > 32 ? 32 : length);
}

static void zero_report(uint8_t *data) {
    memset(data, 0, 32);
}

static uint32_t get_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

static void expect_revisions(uint32_t keymap, uint32_t macro, uint32_t config) {
    EXPECT_EQ(era_state_sync_keymap_revision(), keymap);
    EXPECT_EQ(era_state_sync_macro_revision(), macro);
    EXPECT_EQ(era_state_sync_config_revision(), config);
}

TEST(EraExactMs, GlobalExactRoundTripAndLegacyProjection) {
    era_tapping_init();
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0;
    report[4] = 137;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 137);

    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = 15;
    report[2] = 5;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(report[3], 0);
    EXPECT_EQ(report[4], 137);

    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = 15;
    report[2] = 1;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(report[3], 12);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0;
    report[4] = 1;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 1);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0xFF;
    report[4] = 0xFF;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 65535);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0;
    report[4] = 0;
    EXPECT_FALSE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 65535);
}

TEST(EraExactMs, TapDanceSlotsIndependent) {
    era_tapdance_init();
    const uint16_t values[8] = {101, 137, 141, 163, 187, 203, 499, 500};
    for (uint8_t slot = 0; slot < 8; slot++) {
        uint8_t report[32];
        zero_report(report);
        report[0] = id_custom_set_value;
        report[1] = 0;
        report[2] = (uint8_t)(72 + slot);
        report[3] = (uint8_t)(values[slot] >> 8);
        report[4] = (uint8_t)(values[slot] & 0xFF);
        ASSERT_TRUE(era_tapdance_handle_via_command(report, 32));
        EXPECT_EQ(era_tapdance_get_slot_term_ms(slot), values[slot]);
    }
    EXPECT_EQ(era_tapdance_get_slot_term_ms(0), 101);
    EXPECT_EQ(era_tapdance_get_slot_term_ms(1), 137);
}

TEST(EraExactMs, StateSyncGetEnvelope) {
    uint8_t report[32];
    zero_report(report);
    report[0]       = id_get_keyboard_value;
    report[1]       = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2]       = ERA_STATE_SYNC_ENVELOPE_VERSION;
    report[4]       = 0xAB;
    report[5]       = 0xCD;
    uint32_t before = era_state_sync_config_revision();
    ASSERT_TRUE(era_state_sync_via_command(report, 32));
    EXPECT_EQ(g_last_send[0], id_get_keyboard_value);
    EXPECT_EQ(g_last_send[1], ERA_STATE_SYNC_KEYBOARD_VALUE);
    EXPECT_EQ(g_last_send[2], ERA_STATE_SYNC_ENVELOPE_VERSION);
    EXPECT_EQ(g_last_send[3], ERA_STATE_SYNC_STATUS_OK);
    EXPECT_EQ(g_last_send[4], 0xAB);
    EXPECT_EQ(g_last_send[5], 0xCD);
    EXPECT_EQ(g_last_send[6], ERA_STATE_SYNC_DOMAIN_MASK_INITIAL);
    EXPECT_EQ(get_be32(&g_last_send[8]), era_state_sync_keymap_revision());
    EXPECT_EQ(get_be32(&g_last_send[12]), era_state_sync_macro_revision());
    EXPECT_EQ(get_be32(&g_last_send[16]), era_state_sync_config_revision());
    EXPECT_EQ(era_state_sync_config_revision(), before);
}

TEST(EraExactMs, StateSyncRejectsShortAndInvalidRequestsSafely) {
    uint8_t report[32];
    zero_report(report);
    report[0]    = id_get_keyboard_value;
    report[1]    = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2]    = ERA_STATE_SYNC_ENVELOPE_VERSION;
    g_send_count = 0;
    EXPECT_FALSE(era_state_sync_via_command(report, 31));
    EXPECT_EQ(g_send_count, 0U);

    report[6] = 1;
    ASSERT_TRUE(era_state_sync_via_command(report, 32));
    EXPECT_EQ(g_send_count, 1U);
    EXPECT_EQ(g_last_send[3], ERA_STATE_SYNC_STATUS_INVALID);
}

TEST(EraExactMs, StateSyncReportsUnsupportedEnvelopeVersionOnce) {
    uint8_t report[32];
    zero_report(report);
    report[0]    = id_get_keyboard_value;
    report[1]    = ERA_STATE_SYNC_KEYBOARD_VALUE;
    report[2]    = ERA_STATE_SYNC_ENVELOPE_VERSION + 1;
    report[4]    = 0x12;
    report[5]    = 0x34;
    g_send_count = 0;
    ASSERT_TRUE(era_state_sync_via_command(report, 32));
    EXPECT_EQ(g_send_count, 1U);
    EXPECT_EQ(g_last_send[2], ERA_STATE_SYNC_ENVELOPE_VERSION);
    EXPECT_EQ(g_last_send[3], ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION);
    EXPECT_EQ(g_last_send[4], 0x12);
    EXPECT_EQ(g_last_send[5], 0x34);
}

TEST(EraExactMs, StateSyncProductionNvmMapsKeymapMacroAndLayoutExactlyOnce) {
    uint8_t keymap_data[2] = {0x12, 0x34};
    era_state_sync_set_revisions_for_testing(1, 1, 1);
    nvm_dynamic_keymap_update_buffer(0, sizeof(keymap_data), keymap_data);
    expect_revisions(2, 1, 1);
    nvm_dynamic_keymap_update_buffer(0, sizeof(keymap_data), keymap_data);
    expect_revisions(2, 1, 1);

    uint8_t macro_data[2] = {'A', 0};
    nvm_dynamic_keymap_macro_update_buffer(0, sizeof(macro_data), macro_data);
    expect_revisions(2, 2, 1);
    nvm_dynamic_keymap_macro_update_buffer(0, sizeof(macro_data), macro_data);
    expect_revisions(2, 2, 1);

    nvm_via_update_layout_options(1);
    expect_revisions(2, 2, 2);
    nvm_via_update_layout_options(1);
    expect_revisions(2, 2, 2);
}

TEST(EraExactMs, StateSyncMapsEncoderRangeToKeymap) {
    era_state_sync_set_revisions_for_testing(10, 20, 30);
    era_state_sync_note_eeprom_span(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR - 1, 1);
    expect_revisions(11, 20, 30);
}

TEST(EraExactMs, StateSyncMapsOnlySyncableEraConfig) {
    era_state_sync_set_revisions_for_testing(1, 1, 1);
    uint8_t syncable = 0;
    ASSERT_EQ(era_eeprom_read_config(&syncable, 10, sizeof(syncable)), sizeof(syncable));
    syncable ^= 0x5A;
    ASSERT_EQ(era_eeprom_update_config(&syncable, 10, sizeof(syncable)), sizeof(syncable));
    expect_revisions(1, 1, 2);
    ASSERT_EQ(era_eeprom_update_config(&syncable, 10, sizeof(syncable)), sizeof(syncable));
    expect_revisions(1, 1, 2);

    uint8_t protected_value = 0;
    ASSERT_EQ(era_eeprom_read_config(&protected_value, kEraProtectedConfigStart, sizeof(protected_value)), sizeof(protected_value));
    protected_value ^= 0xA5;
    ASSERT_EQ(era_eeprom_update_config(&protected_value, kEraProtectedConfigStart, sizeof(protected_value)), sizeof(protected_value));
    expect_revisions(1, 1, 2);
}

TEST(EraExactMs, StateSyncMapsCoreEeconfigChangedWritesToConfig) {
    era_state_sync_set_revisions_for_testing(1, 1, 1);

    rgb_config_t rgb;
    nvm_eeconfig_read_rgb_matrix(&rgb);
    rgb.raw ^= UINT64_C(1);
    nvm_eeconfig_update_rgb_matrix(&rgb);
    expect_revisions(1, 1, 2);
    nvm_eeconfig_update_rgb_matrix(&rgb);
    expect_revisions(1, 1, 2);

    keymap_config_t keymap;
    nvm_eeconfig_read_keymap(&keymap);
    keymap.raw ^= 1U;
    nvm_eeconfig_update_keymap(&keymap);
    expect_revisions(1, 1, 3);
    nvm_eeconfig_update_keymap(&keymap);
    expect_revisions(1, 1, 3);

    layer_state_t default_layer = nvm_eeconfig_read_default_layer();
    layer_state_t changed_layer = default_layer == 1 ? 2 : 1;
    nvm_eeconfig_update_default_layer(changed_layer);
    expect_revisions(1, 1, 4);
    nvm_eeconfig_update_default_layer(changed_layer);
    expect_revisions(1, 1, 4);
}

TEST(EraExactMs, StateSyncMapsSevenStorageDomainsToThreeUiDomains) {
    era_state_sync_set_revisions_for_testing(1, 1, 1);
    era_state_sync_note_storage_domain(ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP);
    expect_revisions(2, 1, 1);
    era_state_sync_note_storage_domain(ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO);
    expect_revisions(2, 2, 1);
    const uint8_t config_domains[] = {
        ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG, ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX, ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG, ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER, ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS,
    };
    for (uint8_t domain : config_domains) {
        uint32_t before = era_state_sync_config_revision();
        era_state_sync_note_storage_domain(domain);
        EXPECT_EQ(era_state_sync_config_revision(), before + 1);
        EXPECT_EQ(era_state_sync_keymap_revision(), 2U);
        EXPECT_EQ(era_state_sync_macro_revision(), 2U);
    }
}

TEST(EraExactMs, StateSyncConfigRevisionBumpsOnSetBeforeSave) {
    era_tapping_init();
    era_state_sync_set_revisions_for_testing(1, 1, 1);

    uint8_t  report[32];
    uint32_t before = era_state_sync_config_revision();
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0;
    report[4] = 137;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 137);
    EXPECT_EQ(era_state_sync_config_revision(), before + 1);

    zero_report(report);
    report[0] = id_custom_save;
    report[1] = 15;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_state_sync_config_revision(), before + 1);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0;
    report[4] = 137;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_state_sync_config_revision(), before + 1);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0;
    report[4] = 0;
    EXPECT_FALSE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 137);
    EXPECT_EQ(era_state_sync_config_revision(), before + 1);
}

TEST(EraExactMs, StateSyncDirectPersistedConfigStillBumpsWithoutSetter) {
    era_state_sync_set_revisions_for_testing(1, 1, 1);
    uint8_t syncable = 0;
    ASSERT_EQ(era_eeprom_read_config(&syncable, 10, sizeof(syncable)), sizeof(syncable));
    syncable ^= 0x3C;
    ASSERT_EQ(era_eeprom_update_config(&syncable, 10, sizeof(syncable)), sizeof(syncable));
    expect_revisions(1, 1, 2);
}

TEST(EraExactMs, StateSyncSemanticPersistSuppressionIsRegionScopedAndOneShot) {
    era_tapping_init();
    era_state_sync_set_revisions_for_testing(1, 1, 1);

    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    uint16_t next_term = era_tapping_get_term_ms() == 137 ? 139 : 137;
    report[3]          = (uint8_t)(next_term >> 8);
    report[4]          = (uint8_t)next_term;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    expect_revisions(1, 1, 2);

    uint8_t unrelated = 0;
    ASSERT_EQ(era_eeprom_read_config(&unrelated, 10, sizeof(unrelated)), sizeof(unrelated));
    unrelated ^= 0x5A;
    ASSERT_EQ(era_eeprom_update_config(&unrelated, 10, sizeof(unrelated)), sizeof(unrelated));
    expect_revisions(1, 1, 3);

    zero_report(report);
    report[0] = id_custom_save;
    report[1] = 15;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    expect_revisions(1, 1, 3);

    unrelated ^= 0xA5;
    ASSERT_EQ(era_eeprom_update_config(&unrelated, 10, sizeof(unrelated)), sizeof(unrelated));
    expect_revisions(1, 1, 4);
}

TEST(EraExactMs, StateSyncRevisionWrapSkipsZero) {
    era_state_sync_set_revisions_for_testing(UINT32_MAX, UINT32_MAX, UINT32_MAX);
    era_state_sync_note_eeprom_span(DYNAMIC_KEYMAP_EEPROM_ADDR, 1);
    era_state_sync_note_eeprom_span(DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR, 1);
    era_state_sync_note_config_semantic_commit(0, 1);
    expect_revisions(1, 1, 1);
}

// Legacy global wire: 1-byte dropdown units × 10 ms, 100–500 / 20 ms.
TEST(EraExactMs, LegacyGlobalWireGridRoundTrip) {
    era_tapping_init();
    const struct {
        uint8_t  units;
        uint16_t ms;
    } cases[] = {{10, 100}, {14, 140}, {50, 500}};
    for (const auto &c : cases) {
        uint8_t report[32];
        zero_report(report);
        report[0] = id_custom_set_value;
        report[1] = 15;
        report[2] = 1;
        report[3] = c.units;
        ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
        EXPECT_EQ(era_tapping_get_term_ms(), c.ms);

        zero_report(report);
        report[0] = id_custom_get_value;
        report[1] = 15;
        report[2] = 1;
        ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
        EXPECT_EQ(report[3], c.units);

        zero_report(report);
        report[0] = id_custom_get_value;
        report[1] = 15;
        report[2] = 5;
        ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
        EXPECT_EQ(((uint16_t)report[3] << 8) | report[4], c.ms);
    }
}

TEST(EraExactMs, LegacyGetClampsWithoutMutatingExactStore) {
    era_tapping_init();
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0;
    report[4] = 1;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 1);

    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = 15;
    report[2] = 1;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(report[3], 10);
    EXPECT_EQ(era_tapping_get_term_ms(), 1);

    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 15;
    report[2] = 5;
    report[3] = 0xFF;
    report[4] = 0xFF;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(era_tapping_get_term_ms(), 65535);

    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = 15;
    report[2] = 1;
    ASSERT_TRUE(era_tapping_handle_via_command(report, 32));
    EXPECT_EQ(report[3], 50);
    EXPECT_EQ(era_tapping_get_term_ms(), 65535);
}

TEST(EraExactMs, LegacyTapDanceWireTermRoundTrip) {
    era_tapdance_init();
    uint8_t report[32];
    zero_report(report);
    report[0] = id_custom_set_value;
    report[1] = 0;
    report[2] = 36;
    report[3] = 14;
    ASSERT_TRUE(era_tapdance_handle_via_command(report, 32));
    EXPECT_EQ(era_tapdance_get_slot_term_ms(0), 140);

    zero_report(report);
    report[0] = id_custom_get_value;
    report[1] = 0;
    report[2] = 36;
    ASSERT_TRUE(era_tapdance_handle_via_command(report, 32));
    EXPECT_EQ(report[3], 14);
}
