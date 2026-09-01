// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>

#include "gtest/gtest.h"
#include "test_driver.hpp"

extern "C" {
#include "keyboards/era/comm/riley/riley_common.h"
#define _Static_assert static_assert
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#undef _Static_assert
#include "keyboards/era/common/system/era_board_hooks.h"
#include "keyboards/era/common/system/era_nonsplit_board.h"
#include "keyboard.h"
#include "led.h"
#include "rgblight.h"
}

#ifdef RGBLIGHT_LAYERS_OVERRIDE_RGB_OFF
#    error Riley must not render indicator layers while RGBLight is disabled/suspended.
#endif

namespace {

std::array<uint8_t, ERA_EEPROM_CONFIG_SIZE> g_eeprom{};
std::array<bool, RILEY_INDICATOR_SLOT_COUNT> g_layer_state{};
bool     g_rgblight_enabled;
bool     g_velocikey_enabled;
uint32_t g_rgblight_enable_calls;
uint32_t g_eeprom_update_calls;
uint16_t g_last_semantic_offset;
uint16_t g_last_semantic_length;
uint32_t g_semantic_calls;
uint32_t g_led_ports_calls;
led_t    g_last_port_leds;

constexpr uint16_t kRileyConfigOffset = ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET;
constexpr uint16_t kRileyConfigSize   = 10;

led_t lock_state(bool caps = false, bool scroll = false, bool num = false) {
    led_t state{};
    state.caps_lock   = caps;
    state.scroll_lock = scroll;
    state.num_lock    = num;
    return state;
}

void via_set_u8(uint8_t id, uint8_t value) {
    uint8_t data[3] = {id, value, 0};
    ASSERT_TRUE(era_board_via_set_value(data));
}

void via_set_color(uint8_t id, uint8_t hue, uint8_t sat) {
    uint8_t data[3] = {id, hue, sat};
    ASSERT_TRUE(era_board_via_set_value(data));
}

const rgblight_segment_t &segment(uint8_t slot) {
    EXPECT_NE(rgblight_layers, nullptr);
    EXPECT_LT(slot, RILEY_INDICATOR_SLOT_COUNT);
    return rgblight_layers[slot][0];
}

class RileyRgbIndicator : public testing::Test {
   protected:
    TestDriver driver;

    void SetUp() override {
        g_eeprom.fill(0);
        g_layer_state.fill(false);
        g_rgblight_enabled    = true;
        g_velocikey_enabled   = false;
        g_rgblight_enable_calls = 0;
        g_eeprom_update_calls = 0;
        g_last_semantic_offset = 0;
        g_last_semantic_length = 0;
        g_semantic_calls        = 0;
        g_led_ports_calls       = 0;
        g_last_port_leds.raw    = 0;
        driver.set_leds(0);

        era_board_config_load();
        keyboard_post_init_kb();
        g_eeprom_update_calls = 0;
        g_semantic_calls      = 0;
    }

    void set_host_leds(led_t state) {
        driver.set_leds(state.raw);
        ASSERT_TRUE(led_update_kb(state));
    }
};

} // namespace

extern "C" {

const rgblight_segment_t *const *rgblight_layers;

void rgblight_set_layer_state(uint8_t layer, bool enabled) {
    if (layer < g_layer_state.size()) {
        g_layer_state[layer] = enabled;
    }
}

bool rgblight_is_enabled(void) {
    return g_rgblight_enabled;
}

void rgblight_enable(void) {
    g_rgblight_enabled = true;
    g_rgblight_enable_calls++;
}

bool rgblight_velocikey_enabled(void) {
    return g_velocikey_enabled;
}

void rgblight_velocikey_toggle(void) {
    g_velocikey_enabled = !g_velocikey_enabled;
}

void preprocess_rgblight(void) {}

bool process_underglow(uint16_t, keyrecord_t *) {
    return true;
}

void rgblight_suspend(void) {
    g_rgblight_enabled = false;
}

void rgblight_wakeup(void) {}

void rgblight_init(void) {}

void rgblight_task(void) {}

void eeconfig_update_rgblight_default(void) {}

bool process_record_via(uint16_t, keyrecord_t *) {
    return true;
}

bool via_eeprom_is_valid(void) {
    return true;
}

void via_init(void) {}

void eeconfig_init_via(void) {}

void keyboard_post_init_user(void) {}

bool led_update_user(led_t) {
    return true;
}

void led_update_ports(led_t state) {
    g_led_ports_calls++;
    g_last_port_leds = state;
}

uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t length) {
    if (offset + length > g_eeprom.size()) {
        return 0;
    }
    std::memcpy(buf, g_eeprom.data() + offset, length);
    return length;
}

uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t length) {
    if (offset + length > g_eeprom.size()) {
        return 0;
    }
    std::memcpy(g_eeprom.data() + offset, buf, length);
    g_eeprom_update_calls++;
    return length;
}

void era_state_sync_note_config_semantic_commit(uint16_t offset, uint16_t length) {
    g_last_semantic_offset = offset;
    g_last_semantic_length = length;
    g_semantic_calls++;
}

} // extern "C"

TEST_F(RileyRgbIndicator, CleanDefaultsKeepAllThreeSlotsInRgbEffect) {
    EXPECT_EQ(riley_indicator_mode_for_testing(0), RILEY_INDICATOR_RGB_EFFECT);
    EXPECT_EQ(riley_indicator_mode_for_testing(1), RILEY_INDICATOR_RGB_EFFECT);
    EXPECT_EQ(riley_indicator_mode_for_testing(2), RILEY_INDICATOR_RGB_EFFECT);
    EXPECT_FALSE(riley_indicator_only_for_testing());
    EXPECT_EQ(g_layer_state, (std::array<bool, 3>{false, false, false}));

    EXPECT_EQ(g_riley_config.indicator_hsv[0].h, 0U);
    EXPECT_EQ(g_riley_config.indicator_hsv[1].h, 170U);
    EXPECT_EQ(g_riley_config.indicator_hsv[2].h, 85U);
    EXPECT_EQ(g_riley_config.indicator_hsv[0].v, 255U);
    EXPECT_EQ(g_riley_config.indicator_hsv[1].v, 255U);
    EXPECT_EQ(g_riley_config.indicator_hsv[2].v, 255U);
}

TEST_F(RileyRgbIndicator, ThreeDifferentLockModesOverrideOnlyWhileTheirLocksAreActive) {
    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_CAPS_LOCK);
    via_set_u8(id_custom_riley_ind2_mode, RILEY_INDICATOR_SCROLL_LOCK);
    via_set_u8(id_custom_riley_ind3_mode, RILEY_INDICATOR_NUM_LOCK);

    set_host_leds(lock_state(true, false, true));
    EXPECT_EQ(g_layer_state, (std::array<bool, 3>{true, false, true}));
    EXPECT_EQ(segment(0).val, 255U);
    EXPECT_EQ(segment(2).val, 255U);

    set_host_leds(lock_state(false, true, false));
    EXPECT_EQ(g_layer_state, (std::array<bool, 3>{false, true, false}));
    EXPECT_EQ(segment(1).hue, 170U);
}

TEST_F(RileyRgbIndicator, IndicatorOnlyBlacksInactiveLockSlotsButNeverRgbEffectSlots) {
    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_RGB_EFFECT);
    via_set_u8(id_custom_riley_ind2_mode, RILEY_INDICATOR_CAPS_LOCK);
    via_set_u8(id_custom_riley_ind3_mode, RILEY_INDICATOR_NUM_LOCK);
    via_set_u8(id_custom_riley_indicator_only, 1);

    set_host_leds(lock_state(false, false, true));
    EXPECT_EQ(g_layer_state, (std::array<bool, 3>{false, true, true}));
    EXPECT_EQ(segment(1).hue, 0U);
    EXPECT_EQ(segment(1).sat, 0U);
    EXPECT_EQ(segment(1).val, 0U);
    EXPECT_EQ(segment(2).val, 255U);

    via_set_u8(id_custom_riley_indicator_only, 0);
    EXPECT_EQ(g_layer_state, (std::array<bool, 3>{false, false, true}));
}

TEST_F(RileyRgbIndicator, IndividualBrightnessAndColorAreIndependentPerSlot) {
    set_host_leds(lock_state(true, true, true));
    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_CAPS_LOCK);
    via_set_u8(id_custom_riley_ind2_mode, RILEY_INDICATOR_SCROLL_LOCK);
    via_set_u8(id_custom_riley_ind3_mode, RILEY_INDICATOR_NUM_LOCK);
    via_set_color(id_custom_riley_ind1_color, 12, 34);
    via_set_u8(id_custom_riley_ind1_brightness, 56);
    via_set_color(id_custom_riley_ind2_color, 78, 90);
    via_set_u8(id_custom_riley_ind2_brightness, 123);
    via_set_color(id_custom_riley_ind3_color, 145, 167);
    via_set_u8(id_custom_riley_ind3_brightness, 189);

    EXPECT_EQ(segment(0).hue, 12U);
    EXPECT_EQ(segment(0).sat, 34U);
    EXPECT_EQ(segment(0).val, 56U);
    EXPECT_EQ(segment(1).hue, 78U);
    EXPECT_EQ(segment(1).sat, 90U);
    EXPECT_EQ(segment(1).val, 123U);
    EXPECT_EQ(segment(2).hue, 145U);
    EXPECT_EQ(segment(2).sat, 167U);
    EXPECT_EQ(segment(2).val, 189U);
}

TEST_F(RileyRgbIndicator, ModeChangeWhileLockActiveAndReleaseRecoverRgbEffect) {
    set_host_leds(lock_state(true, false, false));
    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_CAPS_LOCK);
    EXPECT_TRUE(g_layer_state[0]);

    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_RGB_EFFECT);
    EXPECT_FALSE(g_layer_state[0]);

    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_SCROLL_LOCK);
    EXPECT_FALSE(g_layer_state[0]);
    set_host_leds(lock_state(false, true, false));
    EXPECT_TRUE(g_layer_state[0]);
    set_host_leds(lock_state(false, false, false));
    EXPECT_FALSE(g_layer_state[0]);
}

TEST_F(RileyRgbIndicator, RuntimeDisabledLikeSleepOrSuspendNeverGetsReenabledByIndicators) {
    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_CAPS_LOCK);
    g_rgblight_enabled     = false;
    g_rgblight_enable_calls = 0;

    set_host_leds(lock_state(true, false, false));
    via_set_u8(id_custom_riley_indicator_only, 1);
    via_set_u8(id_custom_riley_ind1_brightness, 77);

    EXPECT_FALSE(g_rgblight_enabled);
    EXPECT_EQ(g_rgblight_enable_calls, 0U);
}

TEST_F(RileyRgbIndicator, BootRepairsAStoredPersistentRgbOffStateOnlyAtPostInit) {
    g_rgblight_enabled     = false;
    g_rgblight_enable_calls = 0;
    keyboard_post_init_kb();

    EXPECT_TRUE(g_rgblight_enabled);
    EXPECT_EQ(g_rgblight_enable_calls, 1U);
}

TEST_F(RileyRgbIndicator, SaveReloadAndCleanDefaultsUseOneTenByteConfigSpan) {
    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_CAPS_LOCK);
    via_set_u8(id_custom_riley_ind2_mode, RILEY_INDICATOR_SCROLL_LOCK);
    via_set_u8(id_custom_riley_ind3_mode, RILEY_INDICATOR_NUM_LOCK);
    via_set_u8(id_custom_riley_indicator_only, 1);
    via_set_color(id_custom_riley_ind2_color, 31, 41);
    via_set_u8(id_custom_riley_ind2_brightness, 59);

    EXPECT_GT(g_semantic_calls, 0U);
    EXPECT_EQ(g_last_semantic_offset, kRileyConfigOffset);
    EXPECT_EQ(g_last_semantic_length, kRileyConfigSize);

    era_board_via_save();
    std::array<uint8_t, kRileyConfigSize> stored{};
    std::memcpy(stored.data(), g_eeprom.data() + kRileyConfigOffset, stored.size());

    via_set_u8(id_custom_riley_ind1_mode, RILEY_INDICATOR_RGB_EFFECT);
    via_set_u8(id_custom_riley_indicator_only, 0);
    via_set_u8(id_custom_riley_ind2_brightness, 1);
    era_board_config_load();

    EXPECT_EQ(riley_indicator_mode_for_testing(0), RILEY_INDICATOR_CAPS_LOCK);
    EXPECT_EQ(riley_indicator_mode_for_testing(1), RILEY_INDICATOR_SCROLL_LOCK);
    EXPECT_EQ(riley_indicator_mode_for_testing(2), RILEY_INDICATOR_NUM_LOCK);
    EXPECT_TRUE(riley_indicator_only_for_testing());
    EXPECT_EQ(g_riley_config.indicator_hsv[1].h, 31U);
    EXPECT_EQ(g_riley_config.indicator_hsv[1].s, 41U);
    EXPECT_EQ(g_riley_config.indicator_hsv[1].v, 59U);
    EXPECT_EQ(std::memcmp(stored.data(), &g_riley_config, stored.size()), 0);

    era_board_config_reset();
    EXPECT_EQ(riley_indicator_mode_for_testing(0), RILEY_INDICATOR_RGB_EFFECT);
    EXPECT_EQ(riley_indicator_mode_for_testing(1), RILEY_INDICATOR_RGB_EFFECT);
    EXPECT_EQ(riley_indicator_mode_for_testing(2), RILEY_INDICATOR_RGB_EFFECT);
    EXPECT_FALSE(riley_indicator_only_for_testing());
}

TEST_F(RileyRgbIndicator, Gp25LockLedPathRemainsIndependentAndRgbToggleKeycodeIsBlocked) {
    led_t caps = lock_state(true, false, false);
    set_host_leds(caps);
    EXPECT_EQ(g_led_ports_calls, 1U);
    EXPECT_TRUE(g_last_port_leds.caps_lock);

    keyrecord_t record{};
    record.event.pressed = true;
    EXPECT_FALSE(era_board_process_record(QK_UNDERGLOW_TOGGLE, &record));
    EXPECT_TRUE(era_board_process_record(KC_A, &record));
}

TEST_F(RileyRgbIndicator, VelocikeyIsAvailableWithoutChangingIndicatorStorage) {
    uint8_t set_data[3] = {id_custom_riley_velocikey_enable, 1, 0};
    ASSERT_TRUE(era_board_via_set_value(set_data));
    EXPECT_TRUE(g_velocikey_enabled);
    EXPECT_EQ(set_data[1], 1U);

    uint8_t get_data[3] = {id_custom_riley_velocikey_enable, 0, 0};
    ASSERT_TRUE(era_board_via_get_value(get_data));
    EXPECT_EQ(get_data[1], 1U);
}
