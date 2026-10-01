// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
// Actual matrix producers, ERA exact setting, LT/MT queue and layer APIs.
TEST_F(EraBacklightTapHoldTiming, ExactGlobalTermSurvivesSkippedWrapWindow) {
    TestDriver driver;
    unsigned taps = 0;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([&](report_keyboard_t& report) {
            if (std::find(std::begin(report.keys), std::end(report.keys), KC_A) != std::end(report.keys)) ++taps;
        }));
    for (uint32_t start : {200000U, UINT32_MAX - 30000U}) for (uint16_t term : {500U, 501U, 32767U, 32768U, 65534U, 65535U}) for (bool mt : {false, true}) {
        SCOPED_TRACE(testing::Message() << "start=" << start << " term=" << term << " mt=" << mt);
        const uint16_t code = mt ? LCTL_T(KC_A) : LT(1, KC_A);
        auto key = KeymapKey(0,1,0,code);
        set_keymap({key, KeymapKey(1,1,0,code)});
        ASSERT_TRUE(era_tapping_set_term_ms_exact(term));
        set_time(start-10U); run_one_scan_loop();
        set_time(start); key.press(); run_one_scan_loop();
        set_time(start+term-2U); run_one_scan_loop();
        EXPECT_EQ(layer_state,0U); EXPECT_EQ(get_mods(),0U);
        set_time(start+65536U); run_one_scan_loop();
        EXPECT_EQ(layer_state, mt ? 0U : 2U);
        EXPECT_EQ(get_mods(), mt ? MOD_BIT(KC_LCTL) : 0U);
        key.release(); run_one_scan_loop();
        EXPECT_EQ(layer_state,0U); EXPECT_EQ(get_mods(),0U);
    }
    EXPECT_EQ(taps,0U);
    ASSERT_TRUE(era_tapping_set_term_ms_exact(TAPPING_TERM));
}

TEST_F(EraBacklightTapHoldTiming, LayerOverrideRetiresOnlyLayerContributions) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto td=KeymapKey(0,1,0,TD(0)), mod=KeymapKey(0,2,0,TD(1));
    auto usage=KeymapKey(0,3,0,KC_B), to=KeymapKey(0,4,0,TO(1));
    set_keymap({td,mod,usage,to,KeymapKey(2,2,0,TD(1)),KeymapKey(2,3,0,KC_B),KeymapKey(2,4,0,TO(1))});
    ASSERT_TRUE(era_tapdance_set_action(0,1,MO(2)));
    ASSERT_TRUE(era_tapdance_set_action(1,1,KC_LCTL));
    td.press(); run_one_scan_loop(); idle_for(201);
    mod.press(); run_one_scan_loop(); idle_for(201);
    usage.press(); run_one_scan_loop();
    ASSERT_EQ(layer_state,4U);
    to.press(); run_one_scan_loop(); to.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state,2U);
    EXPECT_EQ(get_mods(),MOD_BIT(KC_LCTL)); EXPECT_TRUE(is_key_pressed(KC_B));
    layer_on(2); td.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state,6U); // New ordinary owner is not the retired TD output.
    layer_off(2); mod.release(); run_one_scan_loop(); usage.release(); run_one_scan_loop();
    EXPECT_EQ(get_mods(),0U); EXPECT_FALSE(is_key_pressed(KC_B)); layer_clear();
    td.press(); run_one_scan_loop(); idle_for(201); EXPECT_EQ(layer_state,4U);
    td.release(); run_one_scan_loop(); EXPECT_EQ(layer_state,0U);
}

TEST_F(EraBacklightTapHoldTiming, LayerClearRetiresOnlyLayerContributions) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto td=KeymapKey(0,1,0,TD(0)); set_keymap({td});
    td.press(); run_one_scan_loop(); idle_for(201);
    register_code(KC_B); register_mods(MOD_BIT(KC_LCTL));
    layer_clear(); EXPECT_EQ(layer_state,0U);
    EXPECT_TRUE(is_key_pressed(KC_B)); EXPECT_EQ(get_mods(),MOD_BIT(KC_LCTL));
    layer_on(1); td.release(); run_one_scan_loop(); EXPECT_EQ(layer_state,2U);
    unregister_code(KC_B); unregister_mods(MOD_BIT(KC_LCTL)); layer_clear();
}

TEST_F(EraBacklightTapHoldTiming, TriLayerDoesNotRecaptureTDContribution) {
    TestDriver driver; EXPECT_NO_REPORT(driver);
    auto td=KeymapKey(0,1,0,TD(0)); set_keymap({td});
    td.press(); run_one_scan_loop(); idle_for(201);
    update_tri_layer(1,2,3); EXPECT_EQ(layer_state,2U);
    td.release(); run_one_scan_loop(); EXPECT_EQ(layer_state,0U);
}

TEST_F(EraBacklightTapHoldTiming, TriLayerPreservesRealSharedContribution) {
    TestDriver driver; EXPECT_NO_REPORT(driver);
    for (bool td_first : {false,true}) {
        auto td=KeymapKey(0,1,0,TD(0)), mo=KeymapKey(0,2,0,MO(1));
        set_keymap({td,mo,KeymapKey(1,1,0,TD(0)),KeymapKey(1,2,0,MO(1))});
        td.press(); run_one_scan_loop(); idle_for(201); mo.press(); run_one_scan_loop();
        update_tri_layer(1,2,3);
        if(td_first) {td.release();run_one_scan_loop();EXPECT_EQ(layer_state,2U);mo.release();run_one_scan_loop();}
        else {mo.release();run_one_scan_loop();EXPECT_EQ(layer_state,2U);td.release();run_one_scan_loop();}
        EXPECT_EQ(layer_state,0U);
    }
}

TEST_F(EraBacklightTapHoldTiming, TriLayerAdjustUsesCombinedStateWithoutAdoptingSources) {
    TestDriver driver; EXPECT_NO_REPORT(driver);
    auto td=KeymapKey(0,1,0,TD(0));set_keymap({td});
    td.press();run_one_scan_loop();idle_for(201);layer_on(2);
    update_tri_layer(1,2,3); EXPECT_EQ(layer_state,14U);
    td.release();run_one_scan_loop();update_tri_layer(1,2,3);EXPECT_EQ(layer_state,4U);
    layer_off(2);update_tri_layer(1,2,3);EXPECT_EQ(layer_state,0U);
}
