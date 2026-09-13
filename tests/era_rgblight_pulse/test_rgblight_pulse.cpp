// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <deque>

#include "gtest/gtest.h"

extern "C" {
#include "ch.h"
#include "keyboards/era/common/features/era_rgblight_pulse.h"
}

namespace {

/* RGBLight stubbed at its public boundary: the engine reads the live config
   through the getters and writes the effect range through the two range
   setters, and this records what it asked the chain to show. */
uint8_t  g_mode;
bool     g_enabled;
uint8_t  g_hue;
uint8_t  g_sat;
uint8_t  g_val;
uint8_t  g_speed;
uint32_t g_writes;
bool     g_shown_on;
uint8_t  g_shown_hue;
uint8_t  g_shown_sat;
uint8_t  g_shown_val;

virtual_timer_t   *g_pulse_timer;
animation_status_t g_anim;
std::deque<uint8_t> g_keys;
uint8_t g_next_key;

/* `rgblight_mode_eeprom_helper()` restarts the animation on every mode entry;
   `rgblight_timer_task()` hands that to the effect as pos16 == 0. */
void select_mode(uint8_t mode) {
    g_mode       = mode;
    g_anim.pos16 = 0;
    era_rgblight_pulse_reset();
}

void tick(void) {
    era_rgblight_pulse_effect(&g_anim);
}

void press(void) {
    uint8_t key = g_next_key++;
    g_keys.push_back(key);
    era_rgblight_pulse_note_key_event(0, key, true);
    tick();
}

void release(void) {
    uint8_t key = g_keys.empty() ? 0 : g_keys.front();
    if (!g_keys.empty()) { g_keys.pop_front(); }
    era_rgblight_pulse_note_key_event(0, key, false);
    tick();
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

void expire_pulse(void) {
    signal_pulse_expiry();
    tick();
}

class EraRgblightPulse : public testing::Test {
   protected:
    void SetUp() override {
        g_keys.clear();
        g_next_key = 0;
        g_mode        = RGBLIGHT_MODE_STATIC_LIGHT;
        g_enabled     = true;
        g_hue         = 10;
        g_sat         = 200;
        g_val         = 120;
        g_speed       = 15;
        g_writes      = 0;
        g_shown_on    = false;
        g_shown_hue   = 0;
        g_shown_sat   = 0;
        g_shown_val   = 0;
        g_pulse_timer = nullptr;
        std::memset(&g_anim, 0, sizeof(g_anim));
        era_rgblight_pulse_init();
        ASSERT_NE(g_pulse_timer, nullptr);
    }
};

} // namespace

extern "C" {

rgblight_ranges_t rgblight_ranges = {0, 4, 0, 4, 4};

void era_rgblight_test_note_timer(virtual_timer_t *vtp) {
    g_pulse_timer = vtp;
}

uint8_t rgblight_get_mode(void) {
    return g_mode;
}

bool rgblight_is_enabled(void) {
    return g_enabled;
}

uint8_t rgblight_get_hue(void) {
    return g_hue;
}

uint8_t rgblight_get_sat(void) {
    return g_sat;
}

uint8_t rgblight_get_val(void) {
    return g_val;
}

uint8_t rgblight_get_speed(void) {
    return g_speed;
}

void rgblight_sethsv_range(uint8_t hue, uint8_t sat, uint8_t val, uint8_t start, uint8_t end) {
    EXPECT_EQ(start, 0U);
    EXPECT_EQ(end, 4U);
    g_writes++;
    g_shown_on  = true;
    g_shown_hue = hue;
    g_shown_sat = sat;
    g_shown_val = val;
}

void rgblight_setrgb(uint8_t r, uint8_t g, uint8_t b) {
    EXPECT_EQ(r, 0U);
    EXPECT_EQ(g, 0U);
    EXPECT_EQ(b, 0U);
    g_writes++;
    g_shown_on = false;
}

} // extern "C"

TEST_F(EraRgblightPulse, EffectSpeedIsFiveMsPlusSpeed) {
    g_speed = 0;
    EXPECT_EQ(era_rgblight_pulse_duration_ms(), 5U);
    g_speed = 15;
    EXPECT_EQ(era_rgblight_pulse_duration_ms(), 20U);
    g_speed = 255;
    EXPECT_EQ(era_rgblight_pulse_duration_ms(), 260U);
}

TEST_F(EraRgblightPulse, TheFourModesAreContiguousAndNothingElseIsOne) {
    EXPECT_TRUE(era_rgblight_pulse_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS));
    EXPECT_TRUE(era_rgblight_pulse_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS));
    EXPECT_TRUE(era_rgblight_pulse_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD));
    EXPECT_TRUE(era_rgblight_pulse_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD));
    EXPECT_EQ(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD - RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS, 3);
    EXPECT_FALSE(era_rgblight_pulse_mode(RGBLIGHT_MODE_STATIC_LIGHT));
    EXPECT_FALSE(era_rgblight_pulse_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS - 1));
    EXPECT_FALSE(era_rgblight_pulse_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD + 1));
}

TEST_F(EraRgblightPulse, ModeEntryRendersTheRestingStateOnceAndUnchangedTicksWriteNothing) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS);
    tick();
    EXPECT_EQ(g_writes, 1U);
    EXPECT_TRUE(g_shown_on);
    EXPECT_EQ(g_shown_hue, 10U);
    EXPECT_EQ(g_shown_sat, 200U);
    EXPECT_EQ(g_shown_val, 120U);
    tick();
    tick();
    EXPECT_EQ(g_writes, 1U);

    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    EXPECT_EQ(g_writes, 2U);
    EXPECT_FALSE(g_shown_on);
    tick();
    EXPECT_EQ(g_writes, 2U);
}

TEST_F(EraRgblightPulse, OffPressBlanksOnPressAndRestoresAtExpiry) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS);
    tick();
    press();
    EXPECT_FALSE(g_shown_on);
    EXPECT_TRUE(g_pulse_timer->armed);
    EXPECT_EQ(g_pulse_timer->interval, 20U);
    expire_pulse();
    EXPECT_TRUE(g_shown_on);
    release();
    EXPECT_TRUE(g_shown_on);
    EXPECT_EQ(g_writes, 3U);
}

TEST_F(EraRgblightPulse, OnPressLightsOnPressAndDarkensAtExpiry) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    EXPECT_FALSE(g_shown_on);
    press();
    EXPECT_TRUE(g_shown_on);
    EXPECT_EQ(g_shown_hue, 10U);
    expire_pulse();
    EXPECT_FALSE(g_shown_on);
    release();
    EXPECT_FALSE(g_shown_on);
}

TEST_F(EraRgblightPulse, RepeatedPressRestartsTheIntervalAtTheCurrentSpeed) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    g_speed = 40;
    press();
    virtual_timer_t *timer = g_pulse_timer;
    ASSERT_TRUE(timer->armed);
    EXPECT_EQ(timer->interval, 45U);
    press();
    EXPECT_TRUE(timer->armed);
    EXPECT_EQ(timer->interval, 45U);
    EXPECT_TRUE(g_shown_on);
    expire_pulse();
    EXPECT_FALSE(g_shown_on);
    release();
    release();
    EXPECT_FALSE(g_shown_on);
}

TEST_F(EraRgblightPulse, PressAfterIsrExpiryBeforeTheTickStartsAFreshPulse) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS);
    tick();
    press();
    EXPECT_FALSE(g_shown_on);
    signal_pulse_expiry();

    era_rgblight_pulse_note_key_event(0, 9, true);
    EXPECT_TRUE(g_pulse_timer->armed);
    tick();
    EXPECT_FALSE(g_shown_on);
    expire_pulse();
    EXPECT_TRUE(g_shown_on);
    release();
    release();
}

TEST_F(EraRgblightPulse, HoldShortTapEndsAtTheTimer) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD);
    tick();
    press();
    release();
    EXPECT_FALSE(g_shown_on);
    expire_pulse();
    EXPECT_TRUE(g_shown_on);
}

TEST_F(EraRgblightPulse, HoldLongPressWaitsForTheReleaseAfterTheTimer) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD);
    tick();
    press();
    EXPECT_TRUE(g_shown_on);
    expire_pulse();
    EXPECT_TRUE(g_shown_on);
    release();
    EXPECT_FALSE(g_shown_on);
}

TEST_F(EraRgblightPulse, HoldOverlappingKeysRestoreOnTheLastRelease) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD);
    tick();
    press();
    press();
    EXPECT_FALSE(g_shown_on);
    expire_pulse();
    EXPECT_FALSE(g_shown_on);
    release();
    EXPECT_FALSE(g_shown_on);
    release();
    EXPECT_TRUE(g_shown_on);
}

TEST_F(EraRgblightPulse, ColourChangeReRendersWithTheCurrentPolarity) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD);
    tick();
    press();
    EXPECT_TRUE(g_shown_on);
    EXPECT_EQ(g_shown_hue, 10U);

    g_hue = 99;
    tick();
    EXPECT_TRUE(g_shown_on);
    EXPECT_EQ(g_shown_hue, 99U);

    expire_pulse();
    release();
    EXPECT_FALSE(g_shown_on);

    /* Resting dark: a brightness change re-renders black, never a lit flash. */
    uint32_t before = g_writes;
    g_val           = 50;
    tick();
    EXPECT_FALSE(g_shown_on);
    EXPECT_EQ(g_writes, before + 1U);
    tick();
    EXPECT_EQ(g_writes, before + 1U);
}

TEST_F(EraRgblightPulse, ModeReentryRetiresHeldKeysAndThePendingOneShot) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD);
    tick();
    press();
    EXPECT_TRUE(g_pulse_timer->armed);

    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    EXPECT_FALSE(g_shown_on);
    EXPECT_FALSE(g_pulse_timer->armed);

    /* The key held across the switch synthesizes nothing on its release. */
    release();
    EXPECT_FALSE(g_shown_on);
    EXPECT_FALSE(g_pulse_timer->armed);

    press();
    EXPECT_TRUE(g_shown_on);
    expire_pulse();
    EXPECT_FALSE(g_shown_on);
    release();
}

TEST_F(EraRgblightPulse, AnExpiryPublishedUnderAnotherModeIsDroppedOnReentry) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    press();
    EXPECT_TRUE(g_shown_on);

    signal_pulse_expiry();
    select_mode(RGBLIGHT_MODE_STATIC_LIGHT);
    release();

    select_mode(RGBLIGHT_MODE_ERA_PULSE_ON_PRESS);
    tick();
    EXPECT_FALSE(g_shown_on);
    uint32_t before = g_writes;
    tick();
    EXPECT_EQ(g_writes, before);
}

TEST_F(EraRgblightPulse, PressesAreIgnoredOutsideAPulseModeOrWithRgblightOff) {
    select_mode(RGBLIGHT_MODE_STATIC_LIGHT);
    era_rgblight_pulse_note_key_event(0, 9, true);
    EXPECT_FALSE(g_pulse_timer->armed);
    era_rgblight_pulse_note_key_event(0, 9, false);
    EXPECT_EQ(g_writes, 0U);

    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS);
    tick();
    EXPECT_EQ(g_writes, 1U);
    g_enabled = false;
    era_rgblight_pulse_note_key_event(0, 9, true);
    EXPECT_FALSE(g_pulse_timer->armed);
    era_rgblight_pulse_note_key_event(0, 9, false);
    EXPECT_EQ(g_writes, 1U);
}

TEST_F(EraRgblightPulse, SuspendCancelsThePulseAndResumeRendersTheRestingState) {
    select_mode(RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD);
    tick();
    press();
    EXPECT_FALSE(g_shown_on);
    EXPECT_TRUE(g_pulse_timer->armed);

    era_rgblight_pulse_suspend();
    EXPECT_FALSE(g_pulse_timer->armed);
    era_rgblight_pulse_note_key_event(0, 9, true);
    EXPECT_FALSE(g_pulse_timer->armed);

    era_rgblight_pulse_resume();
    /* rgblight_wakeup() re-enters the mode. */
    g_anim.pos16 = 0;
    tick();
    EXPECT_TRUE(g_shown_on);

    /* The key held into suspend synthesizes nothing on its release. */
    release();
    EXPECT_TRUE(g_shown_on);
    press();
    EXPECT_FALSE(g_shown_on);
    expire_pulse();
    release();
    EXPECT_TRUE(g_shown_on);
}
