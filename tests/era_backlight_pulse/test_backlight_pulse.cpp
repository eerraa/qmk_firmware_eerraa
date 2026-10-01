// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"
#include "keyboard_report_util.hpp"
#include "test_common.hpp"
#include "test_fixture.hpp"
#include "test_keymap_key.hpp"

using testing::_;

extern "C" {
#include "ch.h"
#include "backlight.h"
#include "keyboards/era/common/features/era_backlight.h"
#include "keyboards/era/common/features/era_backlight_lock.h"
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/features/era_tapping.h"
#include "keyboards/era/common/system/era_common_features.h"
#include "timer.h"

extern backlight_config_t backlight_config;
void set_time(uint32_t time);
void era_test_split_peer_press(uint32_t press_ms);
void era_test_split_reset(void);
}

namespace {

/* Mirrors the hard ERA EEPROM config-block ceiling; C++ cannot include the
   C-only layout header because it intentionally uses _Static_assert. */
std::array<uint8_t, 256> g_era_config{};
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
    era_backlight_note_key_event(row, col, true);
}

void release(uint8_t row = 0, uint8_t col = 0) {
    era_backlight_note_key_event(row, col, false);
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

void reset_backlight_harness(void) {
    g_era_config.fill(0);
    g_pwm_level        = 0;
    g_breathing        = false;
    g_breathing_period = 0;
    g_pulse_timer      = nullptr;
    backlight_level_noeeprom(7);
    era_backlight_init();
    era_backlight_task();
}

class EraBacklightPulse : public testing::Test {
   protected:
    void SetUp() override {
        reset_backlight_harness();
        ASSERT_EQ(era_backlight_get_effect(), ERA_BACKLIGHT_EFFECT_STEADY);
        ASSERT_EQ(g_pwm_level, 7U);
    }
};

class EraBacklightTapHoldTiming : public TestFixture {
   protected:
    void SetUp() override {
        reset_backlight_harness();
        /* Each case owns its keymap. A source layer cached by an earlier case
           would make retro tapping look up a layer this case never mapped. */
        for (uint8_t row = 0; row < MATRIX_ROWS; ++row) {
            for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
                update_source_layers_cache((keypos_t){.col = col, .row = row}, 0);
            }
        }
        era_tapdance_init();
        era_tapping_init();
        ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_CAPS));
        ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(1)));
        ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_NO));
        ASSERT_TRUE(era_tapdance_set_action(0, 3, KC_NO));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, TAPPING_TERM));
        era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
        era_backlight_task();
        ASSERT_EQ(g_pwm_level, 7U);
    }
};

} // namespace

extern "C" {

void era_backlight_test_note_timer(virtual_timer_t *vtp) {
    g_pulse_timer = vtp;
}

uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t length) {
    if (offset > g_era_config.size() || length > g_era_config.size() - offset) {
        return 0;
    }
    std::memcpy(buf, g_era_config.data() + offset, length);
    return length;
}

uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t length) {
    if (offset > g_era_config.size() || length > g_era_config.size() - offset) {
        return 0;
    }
    std::memcpy(g_era_config.data() + offset, buf, length);
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

void switch_event_kb(uint8_t row, uint8_t col, bool pressed) {
    era_common_features_switch_event(row, col, pressed);
    switch_event_user(row, col, pressed);
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (!era_common_features_process_record(keycode, record)) {
        return false;
    }
    return process_record_user(keycode, record);
}

void era_usb_session_task(void) {}

void housekeeping_task_kb(void) {
    /* The production ERA class skeleton runs the common feature task on every
       housekeeping pass; keep that apply stage in the integration fixture. */
    era_common_features_task();
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

TEST_F(EraBacklightPulse, ReleaseFromBeforeModeChangeCannotEndANewerHold) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD);
    era_backlight_task();
    press(0, 0);
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
    era_backlight_task();
    press(0, 1);
    expire_pulse();
    release(0, 0);
    EXPECT_EQ(g_pwm_level, 0U);
    release(0, 1);
    EXPECT_EQ(g_pwm_level, 7U);
}

TEST_F(EraBacklightPulse, ReleaseFromBeforeSuspendCannotEndANewerHold) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
    era_backlight_task();
    press(0, 0);
    era_backlight_suspend();
    era_backlight_resume();
    press(0, 1);
    expire_pulse();
    release(0, 0);
    EXPECT_EQ(g_pwm_level, 0U);
    release(0, 1);
    EXPECT_EQ(g_pwm_level, 7U);
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

TEST_F(EraBacklightPulse, PulseSpeedIsTheFullSliderAndFiveMsPlusSpeed) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS);
    era_backlight_task();
    EXPECT_EQ(era_backlight_get_pulse_speed(), 15U);
    EXPECT_EQ(era_backlight_get_pulse_duration_ms(), 20U);
    press();
    EXPECT_EQ(g_pulse_timer->interval, 20U);
    expire_pulse();
    release();

    era_backlight_set_pulse_speed(ERA_BACKLIGHT_SPEED_MIN);
    EXPECT_EQ(era_backlight_get_pulse_duration_ms(), 5U);
    press();
    EXPECT_EQ(g_pulse_timer->interval, 5U);
    expire_pulse();
    release();

    era_backlight_set_pulse_speed(ERA_BACKLIGHT_SPEED_MAX);
    EXPECT_EQ(era_backlight_get_pulse_duration_ms(), 260U);
    press();
    EXPECT_EQ(g_pulse_timer->interval, 260U);
    expire_pulse();
    release();

    uint16_t previous = 0;
    for (unsigned speed = ERA_BACKLIGHT_SPEED_MIN; speed <= ERA_BACKLIGHT_SPEED_MAX; speed++) {
        era_backlight_set_pulse_speed((uint8_t)speed);
        EXPECT_EQ(era_backlight_get_pulse_speed(), speed);
        uint16_t duration = era_backlight_get_pulse_duration_ms();
        EXPECT_EQ(duration, 5U + speed);
        EXPECT_GT(duration, previous);
        previous = duration;
    }
}

TEST_F(EraBacklightPulse, ABlockStoredOnTheRetiredTenStepScaleIsResetToDefaults) {
    era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD);
    era_backlight_set_breathing_period(3);
    era_backlight_set_pulse_speed(7);
    era_backlight_save_config();

    /* The block is valid byte, effect, period, speed; the valid byte is the
       only 0xB2 in an otherwise near-empty image. 0xB1 marked the block whose
       speed meant 1..10 steps of 20 ms. */
    auto valid = std::find(g_era_config.begin(), g_era_config.end(), 0xB2);
    ASSERT_NE(valid, g_era_config.end());
    *valid = 0xB1;

    era_backlight_reload_from_eeprom();
    era_backlight_task();
    EXPECT_EQ(era_backlight_get_effect(), ERA_BACKLIGHT_EFFECT_STEADY);
    EXPECT_EQ(era_backlight_get_breathing_period(), 5U);
    EXPECT_EQ(era_backlight_get_pulse_speed(), 15U);
    EXPECT_EQ(g_pwm_level, 7U);
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


TEST_F(EraBacklightTapHoldTiming, LayerTapHoldPulseStartsOnPhysicalPress) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, LT(1, KC_CAPS));
    set_keymap({key});

    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    EXPECT_EQ(g_pwm_level, 0U);
    expect_layer_state(0);
    testing::Mock::VerifyAndClearExpectations(&driver);

    /* Expiry while the switch is still physically down must keep Hold active. */
    expire_pulse();
    EXPECT_EQ(g_pwm_level, 0U);

    EXPECT_NO_REPORT(driver);
    idle_for(TAPPING_TERM + 1);
    expect_layer_state(1);
    EXPECT_EQ(g_pwm_level, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    key.release();
    run_one_scan_loop();
    expect_layer_state(0);
    EXPECT_EQ(g_pwm_level, 7U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, TapDanceHoldPulseStartsOnSamePhysicalPress) {
    TestDriver driver;
    auto       key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});

    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    EXPECT_EQ(g_pwm_level, 0U);
    expect_layer_state(0);
    testing::Mock::VerifyAndClearExpectations(&driver);

    expire_pulse();
    EXPECT_EQ(g_pwm_level, 0U);

    EXPECT_NO_REPORT(driver);
    idle_for(TAPPING_TERM + 1);
    expect_layer_state(1);
    EXPECT_EQ(g_pwm_level, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    key.release();
    run_one_scan_loop();
    expect_layer_state(0);
    EXPECT_EQ(g_pwm_level, 7U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, LayerTapTapReportsCapsOnReleaseAfterPhysicalPulse) {
    TestDriver driver;
    auto       key = KeymapKey(0, 0, 0, LT(1, KC_CAPS));
    set_keymap({key});

    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    EXPECT_EQ(g_pwm_level, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_REPORT(driver, (KC_CAPS));
    EXPECT_EMPTY_REPORT(driver);
    key.release();
    run_one_scan_loop();
    EXPECT_EQ(g_pwm_level, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, TapHoldOnlySlotDecidesTheTapOnReleaseNotAtTerm) {
    TestDriver driver;
    auto       key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});

    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    EXPECT_EQ(g_pwm_level, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);

    /* Vial's rule: a slot with only On Tap and On Hold has nothing left to
       wait for once the key is up, so the tap leaves on the release itself.
       The physical Pulse is untouched by that decision. */
    EXPECT_REPORT(driver, (KC_CAPS));
    EXPECT_EMPTY_REPORT(driver);
    key.release();
    run_one_scan_loop();
    EXPECT_EQ(g_pwm_level, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(g_pwm_level, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, DoubleTapSlotKeepsTermForOneTapAndDecidesOnTheSecondRelease) {
    TestDriver driver;
    auto       key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_X));

    /* One tap: a second one could still come, so Term decides. */
    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    key.release();
    run_one_scan_loop();
    idle_for(TAPPING_TERM - 2);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_REPORT(driver, (KC_CAPS));
    EXPECT_EMPTY_REPORT(driver);
    idle_for(2);
    testing::Mock::VerifyAndClearExpectations(&driver);

    /* Two taps: the second release is the decision, well before Term. */
    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    key.release();
    run_one_scan_loop();
    key.press();
    run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_REPORT(driver, (KC_X));
    EXPECT_EMPTY_REPORT(driver);
    key.release();
    run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    idle_for(TAPPING_TERM + 1);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, TapOnlySlotIsDecidedAtItsRelease) {
    TestDriver driver;
    auto       key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_NO));

    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);

    /* No later press can change a tap-only outcome, so Term is not waited for. */
    EXPECT_REPORT(driver, (KC_CAPS));
    EXPECT_EMPTY_REPORT(driver);
    key.release();
    run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    idle_for(TAPPING_TERM + 1);
    testing::Mock::VerifyAndClearExpectations(&driver);
}


TEST_F(EraBacklightTapHoldTiming, TapDanceSlotsKeepIndependentDecisionTerms) {
    TestDriver driver;
    auto td0 = KeymapKey(0, 1, 0, TD(0));
    auto td1 = KeymapKey(0, 2, 0, TD(1));

    /* Both slots carry On Double Tap, so a single tap is Term's to decide. */
    ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_B));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, MO(2)));
    ASSERT_TRUE(era_tapdance_set_action(1, 2, KC_Y));
    ASSERT_TRUE(era_tapdance_set_action(1, 3, KC_NO));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, 101));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, 137));
    EXPECT_EQ(tap_dance_get_tapping_term(TD(0), nullptr), 101U);
    EXPECT_EQ(tap_dance_get_tapping_term(TD(1), nullptr), 137U);

    set_keymap({td0, td1});

    EXPECT_NO_REPORT(driver);
    td0.press();
    run_one_scan_loop();
    td0.release();
    run_one_scan_loop();
    idle_for(99);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_REPORT(driver, (KC_CAPS));
    EXPECT_EMPTY_REPORT(driver);
    idle_for(2);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    td1.press();
    run_one_scan_loop();
    td1.release();
    run_one_scan_loop();
    idle_for(135);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_REPORT(driver, (KC_B));
    EXPECT_EMPTY_REPORT(driver);
    idle_for(2);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, ConsumedBacklightKeyStillTriggersPhysicalPulse) {
    TestDriver driver;
    auto       key = KeymapKey(0, 2, 0, QK_BACKLIGHT_OFF);
    set_keymap({key});

    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    EXPECT_EQ(g_pwm_level, 0U);
    EXPECT_TRUE(is_backlight_enabled());
    EXPECT_EQ(get_backlight_level(), 7U);
    testing::Mock::VerifyAndClearExpectations(&driver);

    EXPECT_NO_REPORT(driver);
    key.release();
    run_one_scan_loop();
    /* Hold effects keep their minimum pulse width after a short tap. */
    EXPECT_EQ(g_pwm_level, 0U);
    EXPECT_TRUE(is_backlight_enabled());
    testing::Mock::VerifyAndClearExpectations(&driver);

    expire_pulse();
    EXPECT_EQ(g_pwm_level, 7U);
}

TEST_F(EraBacklightTapHoldTiming, MaximumSlotTermExpiresAcrossTimerWrap) {
    TestDriver driver;
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, UINT16_MAX));
    set_time(UINT32_MAX - 100);
    const uint32_t start = timer_read32();
    EXPECT_NO_REPORT(driver);
    key.press();
    run_one_scan_loop();
    set_time(start + UINT16_MAX);
    run_one_scan_loop();
    expect_layer_state(0);
    run_one_scan_loop();
    expect_layer_state(1);
    key.release();
    run_one_scan_loop();
    expect_layer_state(0);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, EveryViaAliasUsesItsOwnHoldDeadline) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    for (uint8_t slot = 0; slot < ERA_TAP_DANCE_SLOT_COUNT; ++slot) {
        auto key = KeymapKey(0, 1, 0, ERA_TAP_DANCE_KEYCODE_BASE + slot);
        set_keymap({key});
        const uint16_t term = 101 + 53 * slot;
        ASSERT_TRUE(era_tapdance_set_action(slot, 0, KC_A));
        ASSERT_TRUE(era_tapdance_set_action(slot, 1, MO(1)));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(slot, term));
        set_time(65500 + 2000 * slot);
        const uint32_t start = timer_read32();
        key.press();
        run_one_scan_loop();
        set_time(start + term);
        run_one_scan_loop();
        expect_layer_state(0);
        run_one_scan_loop();
        expect_layer_state(1);
        key.release();
        run_one_scan_loop();
        expect_layer_state(0);
    }
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, FinishedReleasePreservesAnotherSlotsDeadline) {
    TestDriver driver;
    auto td0 = KeymapKey(0, 1, 0, TD(0));
    auto td1 = KeymapKey(0, 2, 0, TD(1));
    auto td1_layer = KeymapKey(1, 2, 0, TD(1));
    set_keymap({td0, td1, td1_layer});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, MO(2)));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, 137));
    EXPECT_NO_REPORT(driver);
    td0.press();
    run_one_scan_loop();
    idle_for(TAPPING_TERM + 1);
    expect_layer_state(1);
    td1.press();
    run_one_scan_loop();
    td0.release();
    run_one_scan_loop();
    expect_layer_state(0);
    idle_for(137);
    expect_layer_state(2);
    td1.release();
    run_one_scan_loop();
    expect_layer_state(0);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, SynthesizedTapWidthFollowsQmkOnEveryPath) {
    TestDriver driver;
    /* Caps Lock keeps QMK's compatibility width whether a Layer-Tap or a Tap
       Dance slot synthesized the tap, and the tap leaves on the release: the
       width is never a decision term. On this host platform the weak
       host_keyboard_delay() is QMK's wait, so the width shows here as
       elapsed time; on the ERA transport the same request is a report
       interval, proved in tests/era_hid_report_interval/. */
    for (bool dance : {false, true}) {
        SCOPED_TRACE(dance ? "TD(0)" : "LT(1, KC_CAPS)");
        auto key = KeymapKey(0, 1, 0, dance ? TD(0) : LT(1, KC_CAPS));
        set_keymap({key});
        uint32_t down = 0, up = 0;
        EXPECT_NO_REPORT(driver);
        key.press();
        run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_REPORT(driver, (KC_CAPS)).WillOnce([&](report_keyboard_t&) { down = timer_read32(); });
        EXPECT_EMPTY_REPORT(driver).WillOnce([&](report_keyboard_t&) { up = timer_read32(); });
        key.release();
        run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_EQ(up - down, TAP_HOLD_CAPS_DELAY);
        EXPECT_NO_REPORT(driver);
        idle_for(TAPPING_TERM + 1);
        testing::Mock::VerifyAndClearExpectations(&driver);
    }

    /* Every other keycode is held TAP_CODE_DELAY. */
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_A));
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    uint32_t down = 0, up = 0;
    EXPECT_REPORT(driver, (KC_A)).WillOnce([&](report_keyboard_t&) { down = timer_read32(); });
    EXPECT_EMPTY_REPORT(driver).WillOnce([&](report_keyboard_t&) { up = timer_read32(); });
    key.press();
    run_one_scan_loop();
    key.release();
    run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);
    EXPECT_EQ(up - down, TAP_CODE_DELAY);
}

TEST_F(EraBacklightTapHoldTiming, CapsTripleFallbackUsesSynchronousQmkTapIntervals) {
    TestDriver driver;
    testing::InSequence order;
    for (unsigned i = 0; i < 3; ++i) {
        EXPECT_REPORT(driver, (KC_CAPS));
        EXPECT_EMPTY_REPORT(driver);
    }
    tap_dance_state_t state{};
    state.count = 3;
    state.pressed = true;
    const uint32_t before = timer_read32();
    era_tapdance_on_each_tap(&state, &era_tapdance_user_data[0]);
    EXPECT_EQ(timer_read32() - before, 3U * TAP_HOLD_CAPS_DELAY);
    testing::Mock::VerifyAndClearExpectations(&driver);
}


TEST_F(EraBacklightTapHoldTiming, TwoFinishedHoldsReleaseOnlyTheirOwnOutputs) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    for (bool first_owner_first : {false, true}) {
        SCOPED_TRACE(first_owner_first);
        auto a = KeymapKey(0, 1, 0, TD(0));
        auto b = KeymapKey(0, 2, 0, TD(1));
        auto b_on_a = KeymapKey(1, 2, 0, TD(1));
        set_keymap({a, b, b_on_a});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(1)));
        ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_X));
        ASSERT_TRUE(era_tapdance_set_action(1, 1, MO(2)));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, 10));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, 20));
        a.press(); run_one_scan_loop(); idle_for(11);
        EXPECT_EQ(layer_state, 1U << 1);
        b.press(); run_one_scan_loop(); idle_for(21);
        EXPECT_EQ(layer_state, (1U << 1) | (1U << 2));
        if (first_owner_first) {
            a.release(); run_one_scan_loop();
            EXPECT_EQ(layer_state, 1U << 2);
            b.release(); run_one_scan_loop();
        } else {
            b.release(); run_one_scan_loop();
            EXPECT_EQ(layer_state, 1U << 1);
            a.release(); run_one_scan_loop();
        }
        EXPECT_EQ(layer_state, 0U);
    }
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, SameSlotPhysicalOwnersKeepTheirExecutedOutputs) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto a = KeymapKey(0, 1, 0, TD(0));
    auto b = KeymapKey(0, 2, 0, TD(0));
    auto b_on_a = KeymapKey(1, 2, 0, TD(0));
    set_keymap({a, b, b_on_a});
    a.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 1U << 1);
    // A owns MO(1); changing the shared settings is not permission to replace A's up.
    ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(2)));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, 37));
    b.press(); run_one_scan_loop(); idle_for(38);
    EXPECT_EQ(layer_state, (1U << 1) | (1U << 2));
    b.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 1U << 1);
    a.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, ReconfiguredPhysicalMappingStillReleasesOriginalDance) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto a = KeymapKey(0, 1, 0, TD(0));
    set_keymap({a});
    a.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 1U << 1);
    // Simulate VIA replacing the entry on its cached source layer while held.
    set_keymap({KeymapKey(0, 1, 0, KC_X)});
    a.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    // Keep a pre-fix failure from contaminating the next test's core state.
    if (auto *state = tap_dance_get_state(0)) reset_tap_dance(state);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, ReconfiguredHeldDanceDoesNotReleaseAnotherInputsOutput) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    const uint16_t outputs[] = {KC_X, KC_LSFT, MO(2)};
    for (uint16_t shared : outputs) {
        auto td    = KeymapKey(0, 1, 0, TD(0));
        auto other = KeymapKey(0, 2, 0, shared);
        set_keymap({td, other, KeymapKey(1, 2, 0, shared)});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(1)));
        td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
        other.press(); run_one_scan_loop();
        // VIA replaces the held entry with the output another input still holds.
        set_keymap({KeymapKey(0, 1, 0, shared), other, KeymapKey(1, 1, 0, shared), KeymapKey(1, 2, 0, shared)});
        td.release(); run_one_scan_loop();
        const bool x_down = std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_X) != std::end(keyboard_report->keys);
        EXPECT_EQ(x_down, shared == KC_X);
        EXPECT_EQ(get_mods(), static_cast<uint8_t>(shared == KC_LSFT ? MOD_BIT(KC_LSFT) : 0));
        EXPECT_EQ(keyboard_report->mods, get_mods());
        EXPECT_EQ(layer_state, shared == MO(2) ? 1U << 2 : 0U);
        other.release(); run_one_scan_loop();
        EXPECT_EQ(layer_state, 0U);
        EXPECT_EQ(get_mods(), 0U);
    }
}

static bool hid_output_down(uint16_t code) {
    if (IS_MOUSE_KEYCODE(code)) {
        const report_mouse_t report = mousekey_get_report();
        return code == MS_BTN1 ? (report.buttons & 1) != 0 : report.y != 0;
    }
    if (IS_CONSUMER_KEYCODE(code)) return host_last_consumer_usage() == KEYCODE2CONSUMER(code);
    return host_last_system_usage() == KEYCODE2SYSTEM(code);
}

TEST_F(EraBacklightTapHoldTiming, TdHidUsageFollowsLastOwner) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(driver, send_mouse_mock(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(driver, send_extra_mock(testing::_)).Times(testing::AnyNumber());
    const uint16_t outputs[] = {MS_BTN1, MS_UP, KC_AUDIO_VOL_UP, KC_SYSTEM_SLEEP};
    for (uint16_t shared : outputs) for (bool td_release_first : {false, true}) {
        auto td    = KeymapKey(0, 1, 0, TD(0));
        auto other = KeymapKey(0, 2, 0, shared);
        set_keymap({td, other});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, shared));
        td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
        other.press(); run_one_scan_loop();
        EXPECT_TRUE(hid_output_down(shared));
        (td_release_first ? td : other).release(); run_one_scan_loop();
        EXPECT_TRUE(hid_output_down(shared)) << "released by the first owner";
        (td_release_first ? other : td).release(); run_one_scan_loop();
        EXPECT_FALSE(hid_output_down(shared));
    }
}

TEST_F(EraBacklightTapHoldTiming, TdExtraUsageUpClearsOnlyItsOwnUsage) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(driver, send_extra_mock(testing::_)).Times(testing::AnyNumber());
    const uint16_t down = KEYCODE2CONSUMER(KC_AUDIO_VOL_DOWN);
    for (bool td_first : {false, true}) {
        auto td    = KeymapKey(0, 1, 0, TD(0));
        auto other = KeymapKey(0, 2, 0, td_first ? KC_AUDIO_VOL_DOWN : KC_AUDIO_VOL_UP);
        set_keymap({td, other});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, td_first ? KC_AUDIO_VOL_UP : KC_AUDIO_VOL_DOWN));
        if (td_first) { td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1); other.press(); run_one_scan_loop(); }
        else { other.press(); run_one_scan_loop(); td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1); }
        ASSERT_EQ(host_last_consumer_usage(), down);
        // The earlier input's up must not clear the later input's usage.
        (td_first ? td : other).release(); run_one_scan_loop();
        EXPECT_EQ(host_last_consumer_usage(), down);
        (td_first ? other : td).release(); run_one_scan_loop();
        EXPECT_EQ(host_last_consumer_usage(), 0U);
    }
    // Ordinary keys alone keep QMK's single-slot release.
    auto up = KeymapKey(0, 1, 0, KC_AUDIO_VOL_UP);
    auto dn = KeymapKey(0, 2, 0, KC_AUDIO_VOL_DOWN);
    set_keymap({up, dn});
    up.press(); run_one_scan_loop(); dn.press(); run_one_scan_loop();
    up.release(); run_one_scan_loop();
    EXPECT_EQ(host_last_consumer_usage(), 0U);
    dn.release(); run_one_scan_loop();
}

TEST_F(EraBacklightTapHoldTiming, TdOnOneShotLayerStillDecidesHold) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto td = KeymapKey(0, 1, 0, TD(0));
    set_keymap({td, KeymapKey(1, 1, 0, TD(0))});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(2)));
    set_oneshot_layer(1, ONESHOT_START);
    clear_oneshot_layer_state(ONESHOT_PRESSED);
    ASSERT_EQ(layer_state, 1U << 1);
    // The press consumes the one-shot layer; the dance still decides Tap or Hold.
    td.press(); run_one_scan_loop();
    EXPECT_FALSE(is_oneshot_layer_active());
    idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 1U << 2);
    EXPECT_EQ(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
    td.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, TdTapOnHeldUsageSendsItsOwnPress) {
    TestDriver driver;
    unsigned gaps = 0;
    bool     count_gaps = false;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        if (count_gaps && std::find(std::begin(report.keys), std::end(report.keys), KC_A) == std::end(report.keys)) ++gaps;
    });
    auto td = KeymapKey(0, 1, 0, TD(0));
    auto a  = KeymapKey(0, 2, 0, KC_A);
    set_keymap({td, a});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(1)));
    a.press(); run_one_scan_loop();
    count_gaps = true;
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(gaps, 1U) << "a tap on a held usage is one host press";
    count_gaps = false;
    a.release(); run_one_scan_loop();
    // The same holds for an ordinary LT tap while a TD hold owns the usage.
    auto lt = KeymapKey(0, 2, 0, LT(1, KC_A));
    set_keymap({td, lt});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_A));
    td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    gaps = 0; count_gaps = true;
    lt.press(); run_one_scan_loop(); lt.release(); run_one_scan_loop();
    EXPECT_EQ(gaps, 1U);
    count_gaps = false;
    td.release(); run_one_scan_loop();
    EXPECT_EQ(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
}

TEST_F(EraBacklightTapHoldTiming, OneShotLayerTappedByADanceIsConsumedByItsHold) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto td = KeymapKey(0, 1, 0, TD(0));
    set_keymap({td, KeymapKey(1, 1, 0, TD(0))});
    // Double Hold falls back to Tap (OSL) followed by Hold (A).
    ASSERT_TRUE(era_tapdance_set_action(0, 0, OSL(1)));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(0, 2, KC_B));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, KC_NO));
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop(); idle_for(20);
    td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_TRUE(is_key_pressed(KC_A));
    EXPECT_FALSE(is_oneshot_layer_active());
    EXPECT_EQ(layer_state, 0U);
    td.release(); run_one_scan_loop(); idle_for(20);
    EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, EachKeystrokeOfADanceHeldUsageReachesTheHost) {
    TestDriver driver;
    unsigned downs = 0;
    bool     had_a = false;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        const bool has = std::find(std::begin(report.keys), std::end(report.keys), KC_A) != std::end(report.keys);
        if (has && !had_a) ++downs;
        had_a = has;
    });
    // Rolling: the TD tap A is interrupted by an ordinary A while both are down.
    auto td = KeymapKey(0, 1, 0, TD(0));
    auto a  = KeymapKey(0, 2, 0, KC_A);
    set_keymap({td, a});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(1)));
    td.press(); run_one_scan_loop(); a.press(); run_one_scan_loop();
    td.release(); run_one_scan_loop();
    EXPECT_TRUE(is_key_pressed(KC_A));
    a.release(); run_one_scan_loop(); idle_for(50);
    EXPECT_EQ(downs, 2U);
    // tap_code (macros, send_string, retro tap) while a TD hold owns A.
    set_keymap({td});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_A));
    td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    downs = 0;
    tap_code(KC_A);
    EXPECT_EQ(downs, 1U);
    EXPECT_TRUE(is_key_pressed(KC_A));
    td.release(); run_one_scan_loop();
    EXPECT_FALSE(is_key_pressed(KC_A));
}

TEST_F(EraBacklightTapHoldTiming, QueuedRecordResolvingToAnOrdinaryKeyIsNotRetired) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto lt = KeymapKey(0, 1, 0, LT(2, KC_Z));
    auto k  = KeymapKey(0, 2, 0, TD(0));
    set_keymap({lt, k, KeymapKey(2, 2, 0, KC_C)});
    // The unchanged TD entry was read on layer 0; the LT hold resolves it to C.
    lt.press(); run_one_scan_loop(); idle_for(10);
    k.press(); run_one_scan_loop();
    clear_keyboard();
    idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 1U << 2);
    EXPECT_TRUE(is_key_pressed(KC_C));
    k.release(); run_one_scan_loop(); lt.release(); run_one_scan_loop();
    EXPECT_FALSE(is_key_pressed(KC_C));
    EXPECT_EQ(tap_dance_get_state(0), nullptr);
}

TEST_F(EraBacklightTapHoldTiming, DanceRetiredAcrossTwoBoundariesLeavesNoSeat) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto lt = KeymapKey(0, 1, 0, LT(2, KC_B));
    auto td = KeymapKey(0, 2, 0, TD(0));
    set_keymap({lt, td});
    lt.press(); run_one_scan_loop();
    td.press(); run_one_scan_loop();    // queued before the first boundary
    clear_keyboard();
    td.release(); run_one_scan_loop();  // queued between the boundaries
    clear_keyboard();
    lt.release(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(tap_dance_get_state(0), nullptr);
    EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, ADanceTapReplaysTheModifiersHeldAtItsPress) {
    TestDriver driver;
    uint8_t c_mods = 0xFF;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        if (std::find(std::begin(report.keys), std::end(report.keys), KC_C) != std::end(report.keys)) c_mods = report.mods;
    });
    // Ctrl comes from a TD hold and is released before the tapped dance decides.
    for (bool rolloff : {false, true}) {
        c_mods  = 0xFF;
        auto t0 = KeymapKey(0, 1, 0, TD(0));
        auto t1 = KeymapKey(0, 2, 0, TD(1));
        set_keymap({t0, t1});
        ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_ESC));
        ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_LCTL));
        ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_C));
        ASSERT_TRUE(era_tapdance_set_action(1, 1, rolloff ? KC_X : KC_NO));
        ASSERT_TRUE(era_tapdance_set_action(1, 2, rolloff ? KC_NO : KC_Z));
        ASSERT_TRUE(era_tapdance_set_action(1, 3, KC_NO));
        t0.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
        t1.press(); run_one_scan_loop();
        if (rolloff) { t0.release(); run_one_scan_loop(); t1.release(); run_one_scan_loop(); }
        else { t1.release(); run_one_scan_loop(); t0.release(); run_one_scan_loop(); }
        idle_for(TAPPING_TERM + 1);
        EXPECT_EQ(c_mods, MOD_BIT(KC_LCTL)) << (rolloff ? "rolloff" : "double pending");
        EXPECT_EQ(get_mods() | get_weak_mods(), 0U);
    }
}

TEST_F(EraBacklightTapHoldTiming, APressSupersedesATombstoneWhoseUpWasLost) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto mt = KeymapKey(0, 1, 0, MT(MOD_LCTL, KC_X));
    auto p  = KeymapKey(0, 2, 0, TD(0));
    auto f3 = KeymapKey(0, 3, 0, KC_B), f4 = KeymapKey(0, 4, 0, KC_C), f5 = KeymapKey(0, 5, 0, KC_D), f6 = KeymapKey(0, 6, 0, KC_E);
    auto lt = KeymapKey(0, 8, 0, LT(1, KC_Y));
    set_keymap({mt, p, f3, f4, f5, f6, lt, KeymapKey(1, 1, 0, KC_TRNS), KeymapKey(1, 2, 0, KC_LSFT), KeymapKey(1, 3, 0, KC_TRNS),
                KeymapKey(1, 4, 0, KC_TRNS), KeymapKey(1, 5, 0, KC_TRNS), KeymapKey(1, 6, 0, KC_TRNS), KeymapKey(1, 8, 0, KC_TRNS)});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(1)));
    p.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    ASSERT_EQ(layer_state, 1U << 1);
    // The TD up waits behind an undecided MT; fast typing overflows the buffer.
    mt.press(); run_one_scan_loop(); idle_for(5);
    p.release(); run_one_scan_loop(); idle_for(5);
    for (auto *k : {&f3, &f4, &f5}) { k->press(); run_one_scan_loop(); idle_for(3); }
    for (auto *k : {&f3, &f4, &f5}) { k->release(); run_one_scan_loop(); idle_for(3); }
    f6.press(); run_one_scan_loop(); idle_for(3);
    f6.release(); run_one_scan_loop(); mt.release(); run_one_scan_loop(); idle_for(TAPPING_TERM * 2);
    // The same position, queued behind an LT hold across a clear, resolves to LSFT.
    lt.press(); run_one_scan_loop(); idle_for(5);
    p.press(); run_one_scan_loop(); idle_for(5);
    clear_keyboard();
    idle_for(TAPPING_TERM + 5);
    EXPECT_EQ(get_mods(), MOD_BIT(KC_LSFT));
    p.release(); run_one_scan_loop(); idle_for(5);
    lt.release(); run_one_scan_loop(); idle_for(TAPPING_TERM * 2);
    EXPECT_EQ(get_mods(), 0U);
    EXPECT_EQ(keyboard_report->mods, 0U);
    EXPECT_EQ(tap_dance_get_state(0), nullptr);
    layer_clear();
}

TEST_F(EraBacklightTapHoldTiming, QueuedDanceUsesOriginalPressInsteadOfDispatchTime) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto lt = KeymapKey(0, 1, 0, LT(2, KC_Z));
    auto td = KeymapKey(0, 2, 0, TD(0));
    auto td_on_lt = KeymapKey(2, 2, 0, TD(0));
    set_keymap({lt, td, td_on_lt});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_NO));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, 50));
    lt.press(); run_one_scan_loop(); idle_for(10);
    td.press(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    // This is the real action_tapping waiting buffer, not a TD-only model.
    idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, (1U << 1) | (1U << 2));
    td.release(); run_one_scan_loop();
    lt.release(); run_one_scan_loop();
    if (auto *state = tap_dance_get_state(0)) reset_tap_dance(state);
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, SameMomentaryDanceOutputWaitsForItsLastOwner) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto a = KeymapKey(0, 1, 0, TD(0));
    auto b = KeymapKey(0, 2, 0, TD(0));
    auto b_on_a = KeymapKey(1, 2, 0, TD(0));
    set_keymap({a, b, b_on_a});
    a.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    b.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 1U << 1);
    a.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 1U << 1);
    b.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}


TEST_F(EraBacklightTapHoldTiming, LayerAndCtrlOwnersReleaseInEitherOrder) {
    TestDriver driver;
    for (bool layer_first : {false, true}) {
        SCOPED_TRACE(layer_first);
        auto a = KeymapKey(0, 1, 0, TD(0));
        auto b = KeymapKey(0, 2, 0, TD(1));
        auto b_layer = KeymapKey(1, 2, 0, TD(1));
        set_keymap({a, b, b_layer});
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, 200));
        ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_X));
        ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_LCTL));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, 400));
        EXPECT_NO_REPORT(driver);
        a.press(); run_one_scan_loop(); idle_for(201);
        EXPECT_EQ(layer_state, 1U << 1);
        testing::Mock::VerifyAndClearExpectations(&driver);
        EXPECT_REPORT(driver, (KC_LCTL));
        b.press(); run_one_scan_loop(); idle_for(401);
        EXPECT_EQ(get_mods(), MOD_BIT(KC_LCTL));
        EXPECT_EQ(layer_state, 1U << 1);
        testing::Mock::VerifyAndClearExpectations(&driver);
        if (layer_first) {
            EXPECT_NO_REPORT(driver);
            a.release(); run_one_scan_loop();
            EXPECT_EQ(layer_state, 0U);
            EXPECT_EQ(get_mods(), MOD_BIT(KC_LCTL));
            testing::Mock::VerifyAndClearExpectations(&driver);
            EXPECT_EMPTY_REPORT(driver);
            b.release(); run_one_scan_loop();
        } else {
            EXPECT_EMPTY_REPORT(driver);
            b.release(); run_one_scan_loop();
            EXPECT_EQ(layer_state, 1U << 1);
            EXPECT_EQ(get_mods(), 0U);
            testing::Mock::VerifyAndClearExpectations(&driver);
            EXPECT_NO_REPORT(driver);
            a.release(); run_one_scan_loop();
        }
        EXPECT_EQ(layer_state, 0U);
        EXPECT_EQ(get_mods(), 0U);
        testing::Mock::VerifyAndClearExpectations(&driver);
    }
}

TEST_F(EraBacklightTapHoldTiming, ActionSettingChangesDoNotReplaceAnExecutedHold) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    key.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(layer_state, 1U << 1);
    ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(2)));
    key.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, DistinctSlotsSharingALayerKeepTheRemainingHold) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto a = KeymapKey(0, 1, 0, TD(0));
    auto b = KeymapKey(0, 2, 0, TD(1));
    auto b_layer = KeymapKey(1, 2, 0, TD(1));
    set_keymap({a, b, b_layer});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, MO(1)));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, 37));
    a.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    b.press(); run_one_scan_loop(); idle_for(38);
    EXPECT_EQ(layer_state, 1U << 1);
    a.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 1U << 1);
    b.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}


TEST_F(EraBacklightTapHoldTiming, ReleasePastTermDoesNotBecomeATapAfterScanGap) {
    TestDriver driver;
    auto key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_LCTL));
    for (uint16_t term : {1U, 200U, 65535U}) {
        SCOPED_TRACE(term);
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, term));
        set_time(UINT32_MAX - 20U);
        const uint32_t start = timer_read32();
        EXPECT_NO_REPORT(driver);
        key.press(); run_one_scan_loop();
        testing::Mock::VerifyAndClearExpectations(&driver);
        // No intervening scan: this release is the first chance to service TD.
        set_time(start + term + 1U);
        EXPECT_REPORT(driver, (KC_LCTL));
        EXPECT_EMPTY_REPORT(driver);
        key.release(); run_one_scan_loop();
        EXPECT_EQ(get_mods(), 0U);
        testing::Mock::VerifyAndClearExpectations(&driver);
    }
}

TEST_F(EraBacklightTapHoldTiming, AllEightPhysicalOwnersCanRemainHeld) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    set_keymap({});
    for (uint8_t layer = 0; layer <= 8; ++layer) {
        for (uint8_t slot = 0; slot < 8; ++slot) {
            add_key(KeymapKey(layer, slot + 1, 0, TD(slot)));
        }
    }
    for (uint8_t slot = 0; slot < 8; ++slot) {
        ASSERT_TRUE(era_tapdance_set_action(slot, 0, KC_X));
        ASSERT_TRUE(era_tapdance_set_action(slot, 1, MO(slot + 1)));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(slot, slot + 1));
    }
    for (bool reverse : {false, true}) {
        uint32_t expected = 0;
        for (uint8_t slot = 0; slot < 8; ++slot) {
            KeymapKey key(0, slot + 1, 0, TD(slot));
            key.press(); run_one_scan_loop(); idle_for(slot + 2);
            expected |= 1U << (slot + 1);
            EXPECT_EQ(layer_state, expected);
        }
        for (uint8_t i = 0; i < 8; ++i) {
            const uint8_t slot = reverse ? 7 - i : i;
            KeymapKey key(0, slot + 1, 0, TD(slot));
            key.release(); run_one_scan_loop();
            expected &= ~(1U << (slot + 1));
            EXPECT_EQ(layer_state, expected);
        }
    }
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightTapHoldTiming, QueuedReleasedTapKeepsEventTimeAcrossWrap) {
    TestDriver driver;
    auto lt = KeymapKey(0, 1, 0, LT(2, KC_Z));
    auto td = KeymapKey(0, 2, 0, TD(0));
    auto td_layer = KeymapKey(2, 2, 0, TD(0));
    set_keymap({lt, td, td_layer});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_LCTL));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0, 50));
    set_time(UINT32_MAX - 30U);
    EXPECT_NO_REPORT(driver);
    lt.press(); run_one_scan_loop(); idle_for(10);
    td.press(); run_one_scan_loop(); idle_for(10);
    td.release(); run_one_scan_loop();
    testing::Mock::VerifyAndClearExpectations(&driver);
    // The queued press and release have both aged beyond Term at dispatch,
    // but their original separation still describes a tap, not a hold.
    EXPECT_REPORT(driver, (KC_X));
    EXPECT_EMPTY_REPORT(driver);
    idle_for(TAPPING_TERM + 1);
    testing::Mock::VerifyAndClearExpectations(&driver);
    EXPECT_NO_REPORT(driver);
    lt.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
    EXPECT_EQ(get_mods(), 0U);
    testing::Mock::VerifyAndClearExpectations(&driver);
}

TEST_F(EraBacklightPulse, SleepGateDarkensTheRailAndKeepsTheStoredLevel) {
    era_backlight_set_sleep(true);
    EXPECT_EQ(g_pwm_level, 0U);
    EXPECT_EQ(get_backlight_level(), 7U) << "a save during sleep stores the user's level";
    era_backlight_refresh_output();
    EXPECT_EQ(g_pwm_level, 0U) << "a brightness refresh while asleep stays dark";
    era_backlight_set_sleep(false);
    EXPECT_EQ(g_pwm_level, 7U);
}

#include "test_td_lifetime.h"
#include "test_socd_td.h"

#include "test_caps_feedback.h"
#include "test_layer_time_contract.h"
