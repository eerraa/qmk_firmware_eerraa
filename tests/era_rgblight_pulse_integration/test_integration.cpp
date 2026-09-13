// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include "gtest/gtest.h"
#include "test_common.hpp"
extern "C" {
#include "ch.h"
#include "rgblight.h"
#include "quantum.h"
#include "keyboards/era/common/features/era_rgblight_pulse.h"
#include "keyboards/era/common/features/era_rgb_sleep.h"
}

namespace {
virtual_timer_t *pulse_timer;
std::array<bool, 4> pixels;
uint32_t flushes;
const rgblight_segment_t indicator[] = {{0, 1, 0, 255, 90}, RGBLIGHT_END_SEGMENTS};
const rgblight_segment_t *const layers[] = {indicator, nullptr};
void driver_init() {}
void driver_color(int index, uint8_t r, uint8_t g, uint8_t b) { pixels.at(index) = r || g || b; }
void driver_all(uint8_t r, uint8_t g, uint8_t b) { pixels.fill(r || g || b); }
void driver_flush() { ++flushes; }
void tick() { wait_ms(1); rgblight_task(); }
void expiry() {
    ASSERT_NE(pulse_timer, nullptr);
    ASSERT_TRUE(pulse_timer->armed);
    auto callback = pulse_timer->callback;
    auto arg = pulse_timer->arg;
    pulse_timer->armed = false;
    callback(pulse_timer, arg);
    tick();
}
class EraRgblightPulseIntegration : public testing::Test {
 protected:
    void SetUp() override {
        timer_clear();
        pixels.fill(false);
        flushes = 0;
        era_rgblight_pulse_init();
        rgblight_layers = layers;
        rgblight_set_layer_state(0, false);
        if (!is_rgblight_initialized) { rgblight_init(); }
        rgblight_wakeup();
        rgblight_enable_noeeprom();
        rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
        rgblight_sethsv_noeeprom(10, 200, 120);
        rgblight_set_speed_noeeprom(15);
        keymap_config.era_rgb_sleep_disabled = false;
    }
};
}
extern "C" {
void era_rgblight_test_note_timer(virtual_timer_t *vtp) { pulse_timer = vtp; }
const rgblight_driver_t rgblight_driver = {driver_init, driver_color, driver_all, driver_flush};
}

TEST_F(EraRgblightPulseIntegration, PressBetweenModeSelectionAndFirstTickSurvives) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    EXPECT_TRUE(pixels[0]);
    ASSERT_TRUE(pulse_timer->armed);
    expiry();
    EXPECT_FALSE(pixels[0]);
}

TEST_F(EraRgblightPulseIntegration, DisableEnableDropsOldPulseBeforeAcceptingNewPress) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    rgblight_disable_noeeprom();
    EXPECT_FALSE(pixels[0]);
    rgblight_enable_noeeprom();
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    EXPECT_TRUE(pixels[0]);
    EXPECT_TRUE(pulse_timer->armed);
}

TEST_F(EraRgblightPulseIntegration, ActualSleepCancelsAndWakeRestoresMode) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD);
    tick();
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    rgblight_suspend();
    EXPECT_FALSE(pixels[0]);
    EXPECT_FALSE(pulse_timer->armed);
    rgblight_wakeup();
    tick();
    EXPECT_TRUE(pixels[0]);
}

TEST_F(EraRgblightPulseIntegration, RgbSleepMasterOffPreservesPulseAcrossQuantumSuspend) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS);
    tick();
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    keymap_config.era_rgb_sleep_disabled = true;
    suspend_power_down_quantum();
    EXPECT_TRUE(pulse_timer->armed);
    expiry();
    EXPECT_TRUE(pixels[0]);
    suspend_wakeup_init_quantum();
}

TEST_F(EraRgblightPulseIntegration, UnchangedTicksDoNotFlushAndColourChangeKeepsPolarity) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    auto before = flushes;
    for (int i = 0; i < 10; ++i) { tick(); }
    EXPECT_EQ(flushes, before);
    rgblight_sethsv_noeeprom(80, 100, 70);
    tick();
    EXPECT_FALSE(pixels[0]);
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    EXPECT_TRUE(pixels[0]);
}

TEST_F(EraRgblightPulseIntegration, ReleaseFromBeforeModeChangeCannotEndANewerHold) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD);
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD);
    era_rgblight_pulse_note_key_event(0, 1, true);
    tick();
    expiry();
    era_rgblight_pulse_note_key_event(0, 0, false);
    tick();
    EXPECT_FALSE(pixels[0]);
    era_rgblight_pulse_note_key_event(0, 1, false);
    tick();
    EXPECT_TRUE(pixels[0]);
}

TEST_F(EraRgblightPulseIntegration, DuplicateOrUnmatchedEdgesCannotCorruptHoldCount) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD);
    era_rgblight_pulse_note_key_event(0, 0, true);
    era_rgblight_pulse_note_key_event(0, 0, true);
    era_rgblight_pulse_note_key_event(0, 1, false);
    tick();
    expiry();
    EXPECT_TRUE(pixels[0]);
    era_rgblight_pulse_note_key_event(0, 0, false);
    tick();
    EXPECT_FALSE(pixels[0]);
}

TEST_F(EraRgblightPulseIntegration, LockOverlayChangesRefreshAnIdlePulseWithoutAKeypress) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    EXPECT_FALSE(pixels[0]);
    rgblight_set_layer_state(0, true);
    tick();
    EXPECT_TRUE(pixels[0]);
    EXPECT_FALSE(pixels[1]);
    rgblight_set_layer_state(0, false);
    tick();
    EXPECT_FALSE(pixels[0]);
}

TEST_F(EraRgblightPulseIntegration, OverlayRefreshDoesNotRestartOrCancelAnActiveHold) {
    rgblight_mode_noeeprom(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD);
    era_rgblight_pulse_note_key_event(0, 0, true);
    tick();
    expiry();
    rgblight_set_layer_state(0, true);
    tick();
    rgblight_set_layer_state(0, false);
    tick();
    EXPECT_TRUE(pixels[0]);
    EXPECT_FALSE(pulse_timer->armed);
    era_rgblight_pulse_note_key_event(0, 0, false);
    tick();
    EXPECT_FALSE(pixels[0]);
}
