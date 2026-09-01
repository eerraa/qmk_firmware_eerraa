// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>

#include "gtest/gtest.h"

extern "C" {
#include "ch.h"
#include "backlight.h"
#include "keyboards/era/common/features/era_backlight.h"
#include "keyboards/era/common/features/era_backlight_lock.h"

extern backlight_config_t backlight_config;
}

namespace {

std::array<uint8_t, 4> g_era_config{};
uint8_t  g_pwm_level;
bool     g_breathing;
uint8_t  g_breathing_period;
virtual_timer_t *g_pulse_timer;

keyrecord_t key_event(bool pressed, uint8_t row = 0, uint8_t col = 0) {
    keyrecord_t record{};
    record.event.pressed = pressed;
    record.event.key.row = row;
    record.event.key.col = col;
    return record;
}

void press(uint8_t row = 0, uint8_t col = 0) {
    auto record = key_event(true, row, col);
    ASSERT_TRUE(era_backlight_process_record(KC_A, &record));
}

void release(uint8_t row = 0, uint8_t col = 0) {
    auto record = key_event(false, row, col);
    ASSERT_TRUE(era_backlight_process_record(KC_A, &record));
    era_backlight_task();
}

void expire_pulse(void) {
    virtual_timer_t *timer = g_pulse_timer;
    ASSERT_NE(timer, nullptr);
    ASSERT_TRUE(timer->armed);
    auto callback = timer->callback;
    auto arg      = timer->arg;
    timer->armed  = false;
    ASSERT_NE(callback, nullptr);
    callback(timer, arg);
    era_backlight_task();
}

void signal_pulse_expiry(void) {
    virtual_timer_t *timer = g_pulse_timer;
    ASSERT_NE(timer, nullptr);
    ASSERT_TRUE(timer->armed);
    auto callback = timer->callback;
    auto arg      = timer->arg;
    timer->armed  = false;
    ASSERT_NE(callback, nullptr);
    callback(timer, arg);
}

class EraBacklightPulse : public testing::Test {
   protected:
    void SetUp() override {
        g_era_config.fill(0);
        g_pwm_level        = 0;
        g_breathing        = false;
        g_breathing_period = 0;
        g_pulse_timer      = nullptr;
        backlight_level_noeeprom(7);
        era_backlight_init();
        era_backlight_task();
        ASSERT_EQ(era_backlight_get_effect(), ERA_BACKLIGHT_EFFECT_STEADY);
        ASSERT_EQ(g_pwm_level, 7U);
    }
};

} // namespace

extern "C" {

void era_backlight_test_note_timer(virtual_timer_t *vtp) {
    g_pulse_timer = vtp;
}

uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t length) {
    (void)offset;
    if (length != g_era_config.size()) {
        return 0;
    }
    std::memcpy(buf, g_era_config.data(), length);
    return length;
}

uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t length) {
    (void)offset;
    if (length != g_era_config.size()) {
        return 0;
    }
    std::memcpy(g_era_config.data(), buf, length);
    return length;
}

void backlight_set(uint8_t level) {
    g_pwm_level = level;
}

void breathing_enable(void) {
    g_breathing = true;
}

void breathing_disable(void) {
    g_breathing = false;
    backlight_set(get_backlight_level());
}

bool is_breathing(void) {
    return g_breathing;
}

void breathing_period_set(uint8_t value) {
    g_breathing_period = value;
}

uint8_t get_breathing_period(void) {
    return g_breathing_period;
}

} // extern "C"

TEST_F(EraBacklightPulse, NormalPressExpiresBackToDefault) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS);
    era_backlight_task();
    EXPECT_EQ(g_pwm_level, 7U);

    press();
    EXPECT_EQ(g_pwm_level, 0U);
    expire_pulse();
    EXPECT_EQ(g_pwm_level, 7U);
    release();
}

TEST_F(EraBacklightPulse, RepeatedPressRestartsActivePulse) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS);
    era_backlight_task();
    EXPECT_EQ(g_pwm_level, 0U);

    press(0, 0);
    virtual_timer_t *timer = g_pulse_timer;
    ASSERT_TRUE(timer->armed);
    const uint32_t first_interval = timer->interval;
    press(0, 1);
    EXPECT_TRUE(timer->armed);
    EXPECT_EQ(timer->interval, first_interval);
    EXPECT_EQ(g_pwm_level, 7U);

    expire_pulse();
    EXPECT_EQ(g_pwm_level, 0U);
    release(0, 0);
    release(0, 1);
}

TEST_F(EraBacklightPulse, PressAfterIsrExpiryBeforeTaskStartsFreshPulse) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS);
    era_backlight_task();

    press(0, 0);
    EXPECT_EQ(g_pwm_level, 0U);
    signal_pulse_expiry();

    press(0, 1);
    EXPECT_EQ(g_pwm_level, 0U);
    era_backlight_task();
    EXPECT_EQ(g_pwm_level, 0U);

    expire_pulse();
    EXPECT_EQ(g_pwm_level, 7U);
    release(0, 0);
    release(0, 1);
}

TEST_F(EraBacklightPulse, HoldShortTapEndsAtTimer) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
    era_backlight_task();
    press();
    release();
    EXPECT_EQ(g_pwm_level, 0U);
    expire_pulse();
    EXPECT_EQ(g_pwm_level, 7U);
}

TEST_F(EraBacklightPulse, HoldLongPressWaitsForReleaseAfterTimer) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD);
    era_backlight_task();
    press();
    EXPECT_EQ(g_pwm_level, 7U);
    expire_pulse();
    EXPECT_EQ(g_pwm_level, 7U);
    release();
    EXPECT_EQ(g_pwm_level, 0U);
}

TEST_F(EraBacklightPulse, HoldOverlappingKeysRestoresOnLastRelease) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
    era_backlight_task();
    press(0, 0);
    press(0, 1);
    expire_pulse();
    EXPECT_EQ(g_pwm_level, 0U);
    release(0, 0);
    EXPECT_EQ(g_pwm_level, 0U);
    release(0, 1);
    EXPECT_EQ(g_pwm_level, 7U);
}

TEST_F(EraBacklightPulse, ModeSwitchCancelsActivePulseAndTimer) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
    era_backlight_task();
    press();
    ASSERT_NE(g_pulse_timer, nullptr);
    ASSERT_TRUE(g_pulse_timer->armed);
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_STEADY);
    era_backlight_task();
    EXPECT_FALSE(g_pulse_timer->armed);
    EXPECT_EQ(g_pwm_level, 7U);
    release();
    EXPECT_EQ(g_pwm_level, 7U);
}

TEST_F(EraBacklightPulse, BrightnessRefreshPreservesPulsePolarity) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS);
    era_backlight_task();
    backlight_level_noeeprom(9);
    era_backlight_refresh_output();
    era_backlight_task();
    EXPECT_EQ(g_pwm_level, 0U);

    press();
    EXPECT_EQ(g_pwm_level, 9U);
    backlight_level_noeeprom(4);
    era_backlight_refresh_output();
    era_backlight_task();
    EXPECT_EQ(g_pwm_level, 4U);
}

TEST_F(EraBacklightPulse, SuspendCancelsPulseAndResumeUsesDefaultState) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
    era_backlight_task();
    press();
    ASSERT_NE(g_pulse_timer, nullptr);
    ASSERT_TRUE(g_pulse_timer->armed);

    era_backlight_suspend();
    backlight_level_noeeprom(0);
    EXPECT_FALSE(g_pulse_timer->armed);
    EXPECT_EQ(g_pwm_level, 0U);
    era_backlight_task();
    EXPECT_EQ(g_pwm_level, 0U);

    backlight_level_noeeprom(7);
    era_backlight_resume();
    EXPECT_EQ(g_pwm_level, 7U);
}

TEST_F(EraBacklightPulse, SuspendStopsBreathingAndResumeRestartsConfiguredEffect) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_BREATHING);
    era_backlight_task();
    ASSERT_TRUE(g_breathing);

    era_backlight_suspend();
    EXPECT_FALSE(g_breathing);
    backlight_level_noeeprom(0);
    EXPECT_EQ(g_pwm_level, 0U);

    backlight_level_noeeprom(7);
    era_backlight_resume();
    EXPECT_TRUE(g_breathing);
}

TEST_F(EraBacklightPulse, SaveReloadRestoresConfigAndRetiresRuntimePulse) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD);
    era_backlight_set_breathing_period(8);
    era_backlight_set_pulse_speed(9);
    era_backlight_save_config();
    era_backlight_task();
    press();
    ASSERT_NE(g_pulse_timer, nullptr);
    ASSERT_TRUE(g_pulse_timer->armed);

    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_STEADY);
    era_backlight_set_breathing_period(2);
    era_backlight_set_pulse_speed(1);
    era_backlight_reload_from_eeprom();
    era_backlight_task();

    EXPECT_EQ(era_backlight_get_effect(), ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD);
    EXPECT_EQ(era_backlight_get_breathing_period(), 8U);
    EXPECT_EQ(era_backlight_get_pulse_speed(), 9U);
    EXPECT_FALSE(g_pulse_timer->armed);
    EXPECT_EQ(g_pwm_level, 0U);
}

TEST_F(EraBacklightPulse, LockedIndicatorRailRefusesPersistentOffKeycodes) {
    backlight_level(1);
    auto down = key_event(true);
    EXPECT_FALSE(era_backlight_lock_process_record(QK_BACKLIGHT_DOWN, &down));
    era_backlight_task();
    EXPECT_TRUE(is_backlight_enabled());
    EXPECT_EQ(get_backlight_level(), 1U);

    auto off = key_event(true);
    EXPECT_FALSE(era_backlight_lock_process_record(QK_BACKLIGHT_OFF, &off));
    era_backlight_task();
    EXPECT_TRUE(is_backlight_enabled());
    EXPECT_EQ(get_backlight_level(), 1U);

    auto toggle = key_event(true);
    EXPECT_FALSE(era_backlight_lock_process_record(QK_BACKLIGHT_TOGGLE, &toggle));
    era_backlight_task();
    EXPECT_TRUE(is_backlight_enabled());
    EXPECT_EQ(get_backlight_level(), 1U);
}

TEST_F(EraBacklightPulse, LockedIndicatorRailRepairsDisabledStoredState) {
    backlight_config_t stored{};
    stored.valid  = true;
    stored.enable = false;
    stored.level  = 0;
    eeconfig_update_backlight(&stored);

    era_backlight_lock_init();

    backlight_config_t repaired{};
    eeconfig_read_backlight(&repaired);
    EXPECT_TRUE(repaired.enable);
    EXPECT_EQ(repaired.level, BACKLIGHT_LEVELS);
}
