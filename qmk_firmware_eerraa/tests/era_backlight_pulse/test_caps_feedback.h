// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
extern "C" {
void era_td_caps_led_task(void);
bool era_td_caps_gpio_state(void);
}

TEST_F(EraBacklightTapHoldTiming, IndependentCapsFeedbackDoesNotWaitForTapDanceOrPulse) {
    TestDriver driver;
    unsigned caps_downs = 0, caps_ups = 0;
    bool had_caps = false;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([&](report_keyboard_t& report) {
            const bool caps = std::find(std::begin(report.keys), std::end(report.keys), KC_CAPS) != std::end(report.keys);
            if (caps && !had_caps) ++caps_downs;
            if (!caps && had_caps) ++caps_ups;
            had_caps = caps;
        }));
    const unsigned speeds[] = {0, 15, 255};
    for (bool dance : {false, true}) for (unsigned speed : speeds) for (unsigned phase = 0; phase < 4; ++phase) {
        SCOPED_TRACE(testing::Message() << "dance=" << dance << " speed=" << speed << " phase=" << phase);
        clear_keyboard(); layer_clear();
        const uint16_t code = dance ? TD(0) : LT(1, KC_CAPS);
        set_keymap({KeymapKey(0,1,0,code), KeymapKey(1,1,0,code)});
        ASSERT_TRUE(era_tapdance_set_action(0,0,KC_CAPS));
        ASSERT_TRUE(era_tapdance_set_action(0,1,MO(1)));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(0,200));
        era_backlight_set_effect(ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD);
        era_backlight_set_pulse_speed(speed); era_backlight_task();
        driver.set_leds(0); era_td_caps_led_task();
        ASSERT_FALSE(era_td_caps_gpio_state());
        const unsigned down_before = caps_downs, up_before = caps_ups;
        auto edge = [](bool pressed) {
            keyevent_t e{};
            e.key.col=1; e.key.row=0; e.type=KEY_EVENT;
            e.time=timer_read32(); e.pressed=pressed;
            /* Ordered production ingress, without forcing an RGB task/flush. */
            switch_event_kb(0,1,pressed);
            action_exec(e);
        };
        edge(true); wait_ms(1);
        ASSERT_NE(g_pulse_timer, nullptr);
        EXPECT_EQ(g_pulse_timer->interval, 5U + speed);
        if (phase & 1U) era_common_features_task();
        if (phase >= 2U) { edge(false); if (phase & 1U) era_common_features_task(); }
        ASSERT_FALSE(era_td_caps_gpio_state()); // A Caps usage is not host feedback.
        const uint8_t pwm_before = g_pwm_level;
        driver.set_leds(2); era_td_caps_led_task();
        EXPECT_TRUE(era_td_caps_gpio_state());
        EXPECT_EQ(g_pwm_level, pwm_before);
        driver.set_leds(0); era_td_caps_led_task();
        EXPECT_FALSE(era_td_caps_gpio_state());
        EXPECT_EQ(g_pwm_level, pwm_before);
        if (phase < 2U) edge(false);
        era_common_features_task();
        EXPECT_EQ(caps_downs - down_before, 1U);
        EXPECT_EQ(caps_ups - up_before, 1U);
        if (g_pulse_timer->armed) expire_pulse();
        era_common_features_task();
        EXPECT_EQ(layer_state, 0U);
        EXPECT_EQ(g_pwm_level, 7U);
        EXPECT_FALSE(g_pulse_timer->armed);
        era_common_features_task(); era_td_caps_led_task();
        EXPECT_FALSE(g_pulse_timer->armed);
        EXPECT_FALSE(era_td_caps_gpio_state());
    }
}

TEST_F(EraBacklightTapHoldTiming, ExistingOneshotsAreConsumedByATapDanceTap) {
    TestDriver driver;
    uint8_t a_mods = 0;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([&](report_keyboard_t& report) {
            if (std::find(std::begin(report.keys),std::end(report.keys),KC_A) != std::end(report.keys)) a_mods=report.mods;
        }));
    for (bool layer : {false, true}) {
        auto td=KeymapKey(0,1,0,TD(0));
        set_keymap({td, KeymapKey(1,1,0,TD(0))});
        ASSERT_TRUE(era_tapdance_set_action(0,0,KC_A));
        if (layer) { set_oneshot_layer(1,ONESHOT_START); clear_oneshot_layer_state(ONESHOT_PRESSED); }
        else set_oneshot_mods(MOD_BIT(KC_LCTL));
        td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop();
        if (!layer) EXPECT_EQ(a_mods,MOD_BIT(KC_LCTL));
        EXPECT_EQ(get_mods() | get_oneshot_mods(),0U);
        EXPECT_FALSE(is_oneshot_layer_active());
        EXPECT_EQ(layer_state,0U);
        EXPECT_EQ(std::find(std::begin(keyboard_report->keys),std::end(keyboard_report->keys),KC_A),std::end(keyboard_report->keys));
    }
}
