// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>

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
#include "keyboards/era/common/system/era_common_features.h"
#include "timer.h"

extern backlight_config_t backlight_config;
void set_time(uint32_t time);
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
    (void)row;
    (void)col;
    era_backlight_note_key_event(true);
}

void release(uint8_t row = 0, uint8_t col = 0) {
    (void)row;
    (void)col;
    era_backlight_note_key_event(false);
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
        era_tapdance_init();
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
    era_common_features_switch_event(pressed);
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

TEST_F(EraBacklightTapHoldTiming, TapOnlySlotKeepsQmkTimeoutExactlyAsVialDoes) {
    TestDriver driver;
    auto       key = KeymapKey(0, 1, 0, TD(0));
    set_keymap({key});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_NO));

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
