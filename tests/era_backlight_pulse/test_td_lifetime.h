// Actual QMK core and ERA runtime; only matrix, clock, EEPROM and host are test adapters.
TEST_F(EraBacklightTapHoldTiming, CapturedModifierFollowsItsOriginalOwner) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto a = KeymapKey(0, 1, 0, TD(0));
    auto b = KeymapKey(0, 2, 0, TD(1));
    set_keymap({a, b});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_LCTL));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, MO(2)));
    a.press(); run_one_scan_loop(); idle_for(201);
    b.press(); run_one_scan_loop(); idle_for(201);
    ASSERT_EQ(get_mods() | get_weak_mods(), MOD_BIT(KC_LCTL));
    a.release(); run_one_scan_loop();
    EXPECT_EQ(get_mods() | get_weak_mods(), 0U);
    EXPECT_EQ(keyboard_report->mods, 0U);
    EXPECT_EQ(layer_state, 1U << 2);
    b.release(); run_one_scan_loop();
}

TEST_F(EraBacklightTapHoldTiming, PartiallyOverlappingModifiersKeepOnlyRemainingBits) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    for (bool reverse : {false, true}) {
        auto a = KeymapKey(0, 1, 0, TD(0));
        auto b = KeymapKey(0, 2, 0, TD(1));
        set_keymap({a, b});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, LCTL(KC_LSFT)));
        ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_LCTL));
        a.press(); run_one_scan_loop(); idle_for(201);
        b.press(); run_one_scan_loop(); idle_for(201);
        if (reverse) {
            b.release(); run_one_scan_loop();
            EXPECT_EQ(get_mods() | get_weak_mods(), MOD_BIT(KC_LCTL) | MOD_BIT(KC_LSFT));
            a.release(); run_one_scan_loop();
        } else {
            a.release(); run_one_scan_loop();
            EXPECT_EQ(get_mods() | get_weak_mods(), MOD_BIT(KC_LCTL));
            b.release(); run_one_scan_loop();
        }
        EXPECT_EQ(get_mods() | get_weak_mods(), 0U);
    }
}

TEST_F(EraBacklightTapHoldTiming, PhysicalMomentaryKeyAndDanceKeepTheirOwnLayer) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    for (bool td_first : {false, true}) for (bool td_release_first : {false, true}) {
        auto td = KeymapKey(0, 1, 0, TD(0));
        auto mo = KeymapKey(0, 2, 0, MO(1));
        set_keymap({td, mo, KeymapKey(1,1,0,TD(0)),KeymapKey(1,2,0,MO(1))});
        if (td_first) { td.press(); run_one_scan_loop(); idle_for(201); mo.press(); run_one_scan_loop(); }
        else { mo.press(); run_one_scan_loop(); td.press(); run_one_scan_loop(); idle_for(201); }
        ASSERT_EQ(layer_state, 1U << 1);
        if (td_release_first) {
            td.release(); run_one_scan_loop(); EXPECT_EQ(layer_state, 1U << 1);
            mo.release(); run_one_scan_loop();
        } else {
            mo.release(); run_one_scan_loop(); EXPECT_EQ(layer_state, 1U << 1);
            td.release(); run_one_scan_loop();
        }
        EXPECT_EQ(layer_state, 0U);
    }
}

TEST_F(EraBacklightTapHoldTiming, ClearKeyboardRetiresAnUndecidedDance) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto a = KeymapKey(0, 1, 0, TD(0));
    set_keymap({a});
    a.press(); run_one_scan_loop();
    clear_keyboard();
    idle_for(201);
    EXPECT_EQ(layer_state, 0U);
    a.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, ClearKeyboardDoesNotReplayAQueuedDance) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto lt=KeymapKey(0,1,0,LT(2,KC_Z)); auto td=KeymapKey(0,2,0,TD(0));
    set_keymap({lt,td,KeymapKey(2,2,0,TD(0))});
    lt.press(); run_one_scan_loop(); idle_for(10);
    td.press(); run_one_scan_loop();
    clear_keyboard(); idle_for(401);
    EXPECT_EQ(layer_state & (1U << 1), 0U);
    td.release(); run_one_scan_loop(); lt.release(); run_one_scan_loop();
}

TEST_F(EraBacklightTapHoldTiming, ReinitializationRetiresHoldAndConsumesLateRemappedRelease) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto td=KeymapKey(0,1,0,TD(0)); set_keymap({td});
    td.press(); run_one_scan_loop(); idle_for(201);
    ASSERT_EQ(layer_state,1U << 1);
    era_tapdance_init();
    EXPECT_EQ(layer_state,0U);
    set_keymap({KeymapKey(0,1,0,KC_X)});
    td.release(); run_one_scan_loop(); idle_for(201);
    EXPECT_EQ(layer_state,0U);
}

TEST_F(EraBacklightTapHoldTiming, KeyboardUsageIsNotReleasedWhileAnotherOwnerRemains) {
    TestDriver driver;
    EXPECT_CALL(driver,send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    for (bool td_release_first : {false,true}) {
        auto td=KeymapKey(0,1,0,TD(0)); auto normal=KeymapKey(0,2,0,KC_A);
        set_keymap({td,normal});
        ASSERT_TRUE(era_tapdance_set_action(0,1,KC_A));
        normal.press(); run_one_scan_loop(); td.press(); run_one_scan_loop(); idle_for(201);
        if (td_release_first) {
            td.release(); run_one_scan_loop(); EXPECT_TRUE((std::find(std::begin(keyboard_report->keys),std::end(keyboard_report->keys),KC_A) != std::end(keyboard_report->keys)));
            normal.release(); run_one_scan_loop();
        } else {
            normal.release(); run_one_scan_loop(); EXPECT_TRUE((std::find(std::begin(keyboard_report->keys),std::end(keyboard_report->keys),KC_A) != std::end(keyboard_report->keys)));
            td.release(); run_one_scan_loop();
        }
        EXPECT_FALSE((std::find(std::begin(keyboard_report->keys),std::end(keyboard_report->keys),KC_A) != std::end(keyboard_report->keys)));
    }
}

TEST_F(EraBacklightTapHoldTiming, SharedUsageJoinIsOneKeystrokeAndSurvivesTheFirstRelease) {
    TestDriver driver;
    bool     watch_a = false;
    unsigned missing = 0;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([&](report_keyboard_t& report) {
            if (watch_a && std::find(std::begin(report.keys), std::end(report.keys), KC_A) == std::end(report.keys)) ++missing;
        }));
    for (bool td_first : {false, true}) for (bool td_release_first : {false, true}) {
        auto td = KeymapKey(0, 1, 0, TD(0));
        auto a = KeymapKey(0, 2, 0, KC_A);
        set_keymap({td, a});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_A));
        if (td_first) { td.press(); run_one_scan_loop(); idle_for(201); }
        else { a.press(); run_one_scan_loop(); }
        watch_a = true; missing = 0;
        if (td_first) { a.press(); run_one_scan_loop(); }
        else { td.press(); run_one_scan_loop(); idle_for(201); }
        EXPECT_EQ(missing, 1U) << "the joining down is its own keystroke, as in QMK/Vial";
        missing = 0;
        if (td_release_first) { td.release(); run_one_scan_loop(); }
        else { a.release(); run_one_scan_loop(); }
        EXPECT_EQ(missing, 0U) << "the first up must not release a usage another input holds";
        EXPECT_NE(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
        watch_a = false;
        if (td_release_first) { a.release(); run_one_scan_loop(); }
        else { td.release(); run_one_scan_loop(); }
        EXPECT_EQ(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
    }
}

TEST_F(EraBacklightTapHoldTiming, CancelledQueuedDanceDoesNotDiscardAnUnrelatedWaitingKey) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto lt = KeymapKey(0, 1, 0, LT(2, KC_Z));
    auto td = KeymapKey(0, 2, 0, TD(0));
    auto b = KeymapKey(0, 3, 0, KC_B);
    set_keymap({lt, td, b, KeymapKey(2,2,0,TD(0)), KeymapKey(2,3,0,KC_B)});
    lt.press(); run_one_scan_loop(); idle_for(10);
    td.press(); run_one_scan_loop(); b.press(); run_one_scan_loop();
    clear_keyboard(); idle_for(401);
    EXPECT_EQ(layer_state & (1U << 1), 0U);
    EXPECT_NE(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_B), std::end(keyboard_report->keys));
    td.release(); run_one_scan_loop();
    EXPECT_NE(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_B), std::end(keyboard_report->keys));
    b.release(); run_one_scan_loop(); lt.release(); run_one_scan_loop();
}

TEST_F(EraBacklightTapHoldTiming, CancelledQueuedTapDoesNotReplayAfterBothEdgesWereBuffered) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([](report_keyboard_t& report) {
            EXPECT_EQ(std::find(std::begin(report.keys), std::end(report.keys), KC_CAPS), std::end(report.keys));
        }));
    auto lt = KeymapKey(0, 1, 0, LT(2, KC_Z));
    auto td = KeymapKey(0, 2, 0, TD(0));
    set_keymap({lt, td, KeymapKey(2,2,0,TD(0))});
    lt.press(); run_one_scan_loop(); td.press(); run_one_scan_loop();
    td.release(); run_one_scan_loop(); clear_keyboard();
    idle_for(401); lt.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, LightingSuspendAndWakeDoNotCancelAnInputHold) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto td = KeymapKey(0, 1, 0, TD(0)); set_keymap({td});
    td.press(); run_one_scan_loop(); idle_for(201);
    ASSERT_EQ(layer_state, 1U << 1);
    suspend_power_down_quantum();
    EXPECT_EQ(layer_state, 1U << 1);
    suspend_wakeup_init_quantum();
    EXPECT_EQ(layer_state, 1U << 1);
    td.release(); run_one_scan_loop(); EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, ToggleAndOneshotTapSurviveDanceResetAndReinitialization) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto td = KeymapKey(0, 1, 0, TD(0));
    auto a = KeymapKey(0, 2, 0, KC_A);
    set_keymap({td, a});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, TG(1)));
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop();
    ASSERT_EQ(layer_state, 1U << 1);
    era_tapdance_init(); EXPECT_EQ(layer_state, 1U << 1);
    layer_clear();
    ASSERT_TRUE(era_tapdance_set_action(0, 0, OSM(MOD_LCTL)));
    ASSERT_TRUE(era_tapdance_set_action(0, 1, MO(1)));
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop();
    EXPECT_EQ(get_oneshot_mods(), MOD_BIT(KC_LCTL));
    a.press(); run_one_scan_loop();
    EXPECT_EQ(keyboard_report->mods, MOD_BIT(KC_LCTL));
    a.release(); run_one_scan_loop();
    EXPECT_EQ(get_mods() | get_weak_mods() | get_oneshot_mods(), 0U);
}

TEST_F(EraBacklightTapHoldTiming, ModTapHoldOwnsItsModifierIndependentlyOfAnOrdinaryKey) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    for (bool td_first : {false, true}) for (bool td_release_first : {false, true}) {
        auto td = KeymapKey(0, 1, 0, TD(0)); auto ctrl = KeymapKey(0, 2, 0, KC_LCTL);
        set_keymap({td, ctrl});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, LCTL_T(KC_A)));
        if (td_first) { td.press(); run_one_scan_loop(); idle_for(201); ctrl.press(); run_one_scan_loop(); }
        else { ctrl.press(); run_one_scan_loop(); td.press(); run_one_scan_loop(); idle_for(201); }
        if (td_release_first) { td.release(); run_one_scan_loop(); }
        else { ctrl.release(); run_one_scan_loop(); }
        EXPECT_EQ(get_mods(), MOD_BIT(KC_LCTL));
        if (td_release_first) { ctrl.release(); run_one_scan_loop(); }
        else { td.release(); run_one_scan_loop(); }
        EXPECT_EQ(get_mods() | get_weak_mods(), 0U);
    }
}

TEST_F(EraBacklightTapHoldTiming, EveryMappedPhysicalKeyCanKeepItsOwnDanceHold) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    std::vector<KeymapKey> keys;
    for (uint8_t row = 0; row < MATRIX_ROWS; ++row) for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
        keys.emplace_back(0, col, row, TD(0));
        add_key(keys.back()); add_key(KeymapKey(1, col, row, TD(0)));
    }
    for (auto& key : keys) { key.press(); run_one_scan_loop(); idle_for(201); }
    for (size_t i = 0; i < keys.size(); ++i) {
        keys[i].release(); run_one_scan_loop();
        if (i == keys.size() - 2) EXPECT_EQ(layer_state, 1U << 1);
    }
    EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, CapturedOldRecordsCannotReleaseANewDanceAtTheSamePosition) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    set_keymap({KeymapKey(0,1,0,TD(0)), KeymapKey(1,1,0,TD(0))});
    /* QMK's MAKE_EVENT is C-only (out-of-order designated fields). */
    auto event = [](bool pressed) {
        keyevent_t e{};
        e.key.col = 1; e.key.row = 0;
        e.time = timer_read32(); e.type = KEY_EVENT; e.pressed = pressed;
        return e;
    };
    keyrecord_t old_down{};
    old_down.event = event(true);
    old_down.event.time = timer_read32();
    old_down.tap_dance_epoch_valid = true;
    old_down.tap_dance_epoch = tap_dance_input_epoch();
    ASSERT_TRUE(pre_process_record_quantum(&old_down));
    keyrecord_t old_up = old_down; old_up.event.pressed = false;
    clear_keyboard();
    action_exec(event(true)); idle_for(202); // Include the first tick strictly beyond Term.
    ASSERT_EQ(layer_state, 1U << 1);
    process_record(&old_down); process_record(&old_up);
    EXPECT_EQ(layer_state, 1U << 1);
    action_exec(event(false));
    EXPECT_EQ(layer_state, 0U);
}

TEST_F(EraBacklightTapHoldTiming, QueuedRemappedDanceAndItsLateReleaseCannotAffectANewUsage) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto lt = KeymapKey(0,1,0,LT(2,KC_Z)); auto td = KeymapKey(0,2,0,TD(0)); auto a = KeymapKey(0,3,0,KC_A);
    set_keymap({lt, td, a, KeymapKey(2,2,0,TD(0)), KeymapKey(2,3,0,KC_A)});
    lt.press(); run_one_scan_loop(); td.press(); run_one_scan_loop(); clear_keyboard();
    set_keymap({lt, KeymapKey(0,2,0,KC_A), a, KeymapKey(2,2,0,KC_A), KeymapKey(2,3,0,KC_A)});
    a.press(); run_one_scan_loop(); idle_for(401);
    EXPECT_EQ(layer_state & (1U << 1), 0U);
    td.release(); run_one_scan_loop();
    EXPECT_NE(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
    a.release(); run_one_scan_loop(); lt.release(); run_one_scan_loop();
    EXPECT_EQ(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
}

TEST_F(EraBacklightTapHoldTiming, ReportOnlyEmptyRestoreDoesNotRetireInputOwners) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto td = KeymapKey(0,1,0,TD(0)); auto a = KeymapKey(0,2,0,KC_A);
    set_keymap({td, a});
    ASSERT_TRUE(era_tapdance_set_action(0,1,KC_A));
    a.press(); run_one_scan_loop(); td.press(); run_one_scan_loop(); idle_for(201);
    const report_keyboard_t saved = *keyboard_report;
    clear_keys(); send_keyboard_report();
    *keyboard_report = saved; send_keyboard_report();
    td.release(); run_one_scan_loop();
    EXPECT_NE(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
    a.release(); run_one_scan_loop();
    EXPECT_EQ(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_A), std::end(keyboard_report->keys));
}

TEST_F(EraBacklightTapHoldTiming, ToggledLayerPersistsAfterTheMomentaryDanceReleases) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto td = KeymapKey(0,1,0,TD(0)); auto toggle = KeymapKey(0,2,0,TG(1));
    set_keymap({td, toggle, KeymapKey(1,1,0,TD(0)), KeymapKey(1,2,0,TG(1))});
    td.press(); run_one_scan_loop(); idle_for(201);
    toggle.press(); run_one_scan_loop(); toggle.release(); run_one_scan_loop();
    td.release(); run_one_scan_loop(); EXPECT_EQ(layer_state, 1U << 1);
    toggle.press(); run_one_scan_loop(); toggle.release(); run_one_scan_loop();
    EXPECT_EQ(layer_state, 0U);
}

#ifdef NKRO_ENABLE
extern "C" {
#include "usb_device_state.h"
}
TEST_F(EraBacklightTapHoldTiming, NkroSharedUsageJoinIsOneKeystrokeAndSurvivesTheFirstRelease) {
    TestDriver driver;
    bool     watch_a = false;
    unsigned missing = 0;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(driver, send_nkro_mock(testing::_)).Times(testing::AnyNumber())
        .WillRepeatedly(testing::Invoke([&](report_nkro_t& report) {
            if (watch_a && !(report.bits[KC_A >> 3] & (1U << (KC_A & 7)))) ++missing;
        }));
    const auto old_protocol = usb_device_state_get_protocol();
    const bool old_nkro = keymap_config.nkro;
    usb_device_state_set_protocol(USB_PROTOCOL_REPORT);
    keymap_config.nkro = true;
    for (bool td_first : {false, true}) for (bool td_release_first : {false, true}) {
        auto td = KeymapKey(0,1,0,TD(0)); auto a = KeymapKey(0,2,0,KC_A);
        set_keymap({td, a});
        EXPECT_TRUE(era_tapdance_set_action(0,1,KC_A));
        if (td_first) { td.press(); run_one_scan_loop(); idle_for(201); }
        else { a.press(); run_one_scan_loop(); }
        watch_a = true; missing = 0;
        if (td_first) { a.press(); run_one_scan_loop(); }
        else { td.press(); run_one_scan_loop(); idle_for(201); }
        EXPECT_EQ(missing, 1U);
        missing = 0;
        if (td_release_first) { td.release(); run_one_scan_loop(); }
        else { a.release(); run_one_scan_loop(); }
        EXPECT_EQ(missing, 0U);
        EXPECT_NE(nkro_report->bits[KC_A >> 3] & (1U << (KC_A & 7)), 0U);
        watch_a = false;
        if (td_release_first) { a.release(); run_one_scan_loop(); }
        else { td.release(); run_one_scan_loop(); }
        EXPECT_EQ(nkro_report->bits[KC_A >> 3] & (1U << (KC_A & 7)), 0U);
    }
    clear_keyboard();
    keymap_config.nkro = old_nkro;
    usb_device_state_set_protocol(old_protocol);
}

TEST_F(EraBacklightTapHoldTiming, ReportFormatClearBoundaryConsumesAnOldRemappedRelease) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(driver, send_nkro_mock(testing::_)).Times(testing::AnyNumber());
    auto td = KeymapKey(0,1,0,TD(0)); auto b = KeymapKey(0,2,0,KC_B);
    set_keymap({td, b});
    ASSERT_TRUE(era_tapdance_set_action(0,1,KC_A));
    const auto old_protocol = usb_device_state_get_protocol();
    const bool old_nkro = keymap_config.nkro;
    usb_device_state_set_protocol(USB_PROTOCOL_REPORT); keymap_config.nkro = true;
    td.press(); run_one_scan_loop(); idle_for(201);
    EXPECT_NE(nkro_report->bits[KC_A >> 3] & (1U << (KC_A & 7)), 0U);
    // The actual VIA NKRO setter uses these two clear_keyboard boundaries.
    clear_keyboard(); keymap_config.nkro = false; clear_keyboard();
    EXPECT_EQ(nkro_report->bits[KC_A >> 3] & (1U << (KC_A & 7)), 0U);
    set_keymap({KeymapKey(0,1,0,KC_B), b});
    b.press(); run_one_scan_loop(); td.release(); run_one_scan_loop();
    EXPECT_NE(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_B), std::end(keyboard_report->keys));
    b.release(); run_one_scan_loop();
    EXPECT_EQ(std::find(std::begin(keyboard_report->keys), std::end(keyboard_report->keys), KC_B), std::end(keyboard_report->keys));
    clear_keyboard(); keymap_config.nkro = old_nkro; usb_device_state_set_protocol(old_protocol);
}
#endif

TEST_F(EraBacklightTapHoldTiming, BootReportMatcherStillRejectsWrongKeysAndModifiers) {
    report_keyboard_t expected{}, reordered{}, wrong_key{}, wrong_mod{};
    expected.keys[0] = KC_A; expected.keys[1] = KC_B; expected.mods = MOD_BIT(KC_LCTL);
    reordered = expected; std::swap(reordered.keys[0], reordered.keys[1]);
    wrong_key = expected; wrong_key.keys[1] = KC_C;
    wrong_mod = expected; wrong_mod.mods = MOD_BIT(KC_LSFT);
    EXPECT_TRUE(expected == reordered);
    EXPECT_FALSE(expected == wrong_key);
    EXPECT_FALSE(expected == wrong_mod);
}

TEST_F(EraBacklightTapHoldTiming, AuxiliaryPoolSaturationNeverOverwritesAnExistingOwner) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    std::array<keyrecord_t, 9> records{};
    EXPECT_TRUE(era_tapdance_set_action(1,1,MO(2)));
    for (uint8_t i = 0; i < records.size(); ++i) {
        auto& record = records[i];
        record.event.key.col = i; record.event.key.row = KEYLOC_ENCODER_CW;
        record.event.type = ENCODER_CW_EVENT; record.event.pressed = true;
        record.event.time = timer_read32();
        record.event.time = timer_read32(); record.tap_dance_epoch_valid = true;
        record.tap_dance_epoch = tap_dance_input_epoch();
        // Exercise the bounded non-matrix core directly; current boards have
        // no encoder map, so this is not an encoder-driver integration claim.
        process_tap_dance(i == 8 ? TD(1) : TD(0), &record);
        idle_for(202);
    }
    EXPECT_EQ(layer_state, 1U << 1);
    records[8].event.pressed = false; process_tap_dance(TD(1), &records[8]);
    EXPECT_EQ(layer_state, 1U << 1);
    for (uint8_t i = 0; i < 8; ++i) {
        records[i].event.pressed = false; process_tap_dance(TD(0), &records[i]);
        if (i == 6) EXPECT_EQ(layer_state, 1U << 1);
    }
    EXPECT_EQ(layer_state, 0U);
}

namespace {
bool report_has(const report_keyboard_t &report, uint8_t key) {
    return std::find(std::begin(report.keys), std::end(report.keys), key) != std::end(report.keys);
}
} // namespace

TEST_F(EraBacklightTapHoldTiming, RetroTapOnADanceHoldFollowsTheSettingAtAnyPosition) {
    TestDriver driver;
    unsigned c_downs = 0;
    bool     c_prev  = false;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        const bool c = report_has(report, KC_C);
        c_downs += c && !c_prev;
        c_prev = c;
    });
    struct Case {
        bool     retro;
        bool     other_press;
        uint8_t  td_col;
        uint16_t hold;
        unsigned taps;
    } cases[] = {
        {true, false, 1, MT(MOD_LCTL, KC_C), 1}, {true, false, 0, MT(MOD_LCTL, KC_C), 1}, {true, false, 1, LT(1, KC_C), 1},
        {true, true, 1, MT(MOD_LCTL, KC_C), 0},  {false, false, 1, MT(MOD_LCTL, KC_C), 0},
    };
    for (const auto &c : cases) {
        c_downs    = 0;
        auto td    = KeymapKey(0, c.td_col, 0, TD(1));
        auto other = KeymapKey(0, c.td_col == 0 ? 2 : 0, 0, KC_D);
        set_keymap({td, other});
        era_tapping_set_retro_tapping(c.retro);
        ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_ESC));
        ASSERT_TRUE(era_tapdance_set_action(1, 1, c.hold));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, TAPPING_TERM));
        td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
        if (c.other_press) { other.press(); run_one_scan_loop(); other.release(); run_one_scan_loop(); }
        td.release(); run_one_scan_loop(); idle_for(10);
        EXPECT_EQ(c_downs, c.taps) << "retro " << c.retro << " col " << int(c.td_col) << " other " << c.other_press;
        EXPECT_EQ(get_mods(), 0U);
        EXPECT_EQ(layer_state, 0U);
        EXPECT_EQ(tap_dance_get_state(1), nullptr);
    }
    era_tapping_set_retro_tapping(false);
}

TEST_F(EraBacklightTapHoldTiming, RetroTapKeepsAModifierItsOwnerStillHolds) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto ctrl = KeymapKey(0, 0, 0, KC_LCTL);
    auto mt   = KeymapKey(0, 1, 0, MT(MOD_LSFT, KC_A));
    set_keymap({ctrl, mt});
    era_tapping_set_retro_tapping(true);
    ctrl.press(); run_one_scan_loop();
    mt.press(); run_one_scan_loop(); mt.release(); run_one_scan_loop(); idle_for(TAPPING_TERM + 10);
    mt.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    mt.release(); run_one_scan_loop(); idle_for(10);
    EXPECT_EQ(get_mods(), MOD_BIT(KC_LCTL));
    EXPECT_EQ(keyboard_report->mods, MOD_BIT(KC_LCTL));
    ctrl.release(); run_one_scan_loop();
    EXPECT_EQ(get_mods(), 0U);
    era_tapping_set_retro_tapping(false);
}

TEST_F(EraBacklightTapHoldTiming, DoubleHoldFallbackTapCarriesTheModifiersOfEitherPress) {
    TestDriver driver;
    uint8_t a_mods = 0xFF, x_mods = 0xFF;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        if (report_has(report, KC_A) && a_mods == 0xFF) a_mods = report.mods;
        if (report_has(report, KC_X)) x_mods = report.mods;
    });
    for (bool shift_through_second : {false, true}) {
        a_mods = x_mods = 0xFF;
        auto shift = KeymapKey(0, 0, 0, KC_LSFT);
        auto td    = KeymapKey(0, 1, 0, TD(1));
        set_keymap({shift, td});
        ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_A));
        ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_X));
        ASSERT_TRUE(era_tapdance_set_action(1, 2, KC_Z));
        ASSERT_TRUE(era_tapdance_set_action(1, 3, KC_NO));
        ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, TAPPING_TERM));
        shift.press(); run_one_scan_loop();
        td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop();
        if (!shift_through_second) { shift.release(); run_one_scan_loop(); }
        td.press(); run_one_scan_loop();
        if (shift_through_second) { shift.release(); run_one_scan_loop(); }
        idle_for(TAPPING_TERM + 1);
        EXPECT_EQ(a_mods, MOD_BIT(KC_LSFT)) << "shift through second " << shift_through_second;
        EXPECT_EQ(x_mods, 0U) << "shift through second " << shift_through_second;
        td.release(); run_one_scan_loop(); idle_for(10);
        EXPECT_EQ(get_mods() | get_weak_mods(), 0U);
        EXPECT_EQ(keyboard_report->mods, 0U);
    }
}

TEST_F(EraBacklightTapHoldTiming, DanceActionsRunQuantumKeycodes) {
    TestDriver driver;
    auto td = KeymapKey(0, 1, 0, TD(1));
    set_keymap({td});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, QK_GRAVE_ESCAPE));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_NO));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, TAPPING_TERM));
    EXPECT_REPORT(driver, (KC_ESC));
    EXPECT_EMPTY_REPORT(driver);
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop(); idle_for(10);
    testing::Mock::VerifyAndClearExpectations(&driver);

    /* A hold keeps the quantum keycode down until the dance's own up. */
    ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, QK_GRAVE_ESCAPE));
    EXPECT_REPORT(driver, (KC_ESC));
    td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    testing::Mock::VerifyAndClearExpectations(&driver);
    EXPECT_EMPTY_REPORT(driver);
    td.release(); run_one_scan_loop(); idle_for(10);
    testing::Mock::VerifyAndClearExpectations(&driver);
    EXPECT_EQ(tap_dance_get_state(1), nullptr);
}

TEST_F(EraBacklightTapHoldTiming, ADanceNeverStartsADanceFromItsOutput) {
    TestDriver driver;
    EXPECT_NO_REPORT(driver);
    auto td = KeymapKey(0, 1, 0, TD(1));
    set_keymap({td});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, TD(2)));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_NO));
    ASSERT_TRUE(era_tapdance_set_action(2, 0, KC_B));
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop(); idle_for(TAPPING_TERM * 2);
    EXPECT_EQ(tap_dance_get_state(1), nullptr);
    EXPECT_EQ(tap_dance_get_state(2), nullptr);
}

TEST_F(EraBacklightTapHoldTiming, AKeyboardClearFromADanceOutputRetiresTheOtherDances) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto magic = KeymapKey(0, 1, 0, TD(1));
    auto ctrl  = KeymapKey(0, 2, 0, TD(2));
    set_keymap({magic, ctrl});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, QK_MAGIC_TOGGLE_GUI));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_NO));
    ASSERT_TRUE(era_tapdance_set_action(2, 0, KC_B));
    ASSERT_TRUE(era_tapdance_set_action(2, 1, KC_LCTL));
    const bool no_gui = keymap_config.no_gui;
    ctrl.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    ASSERT_EQ(get_mods(), MOD_BIT(KC_LCTL));
    magic.press(); run_one_scan_loop(); magic.release(); run_one_scan_loop(); idle_for(10);
    /* The clear the magic keycode performs retires the other held dance. */
    EXPECT_NE(keymap_config.no_gui, no_gui);
    EXPECT_EQ(get_mods(), 0U);
    EXPECT_EQ(tap_dance_get_state(1), nullptr);
    ctrl.release(); run_one_scan_loop(); idle_for(TAPPING_TERM * 2);
    EXPECT_EQ(get_mods(), 0U);
    EXPECT_EQ(tap_dance_get_state(2), nullptr);
    keymap_config.no_gui = no_gui;
    eeconfig_update_keymap(&keymap_config);
}

TEST_F(EraBacklightTapHoldTiming, AClearFromADanceOutputKeepsThatDancesOwnHold) {
    TestDriver driver;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    auto td = KeymapKey(0, 1, 0, TD(1));
    set_keymap({td});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, QK_MAGIC_TOGGLE_GUI));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(1, 2, KC_Z));
    ASSERT_TRUE(era_tapdance_set_action(1, 3, KC_NO));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, TAPPING_TERM));
    const bool no_gui = keymap_config.no_gui;
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop();
    td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    /* The Double Hold fallback tapped a magic keycode, whose clear retires
       every other dance; this dance's own hold continues, as in Vial. */
    EXPECT_NE(keymap_config.no_gui, no_gui);
    EXPECT_TRUE(report_has(*keyboard_report, KC_X));
    ASSERT_NE(tap_dance_get_state(1), nullptr);
    /* Its up, newer than the clear, is still this dance's owned release. */
    set_keymap({KeymapKey(0, 1, 0, KC_B)});
    td.release(); run_one_scan_loop(); idle_for(10);
    EXPECT_FALSE(report_has(*keyboard_report, KC_X));
    EXPECT_EQ(tap_dance_get_state(1), nullptr);
    keymap_config.no_gui = no_gui;
    eeconfig_update_keymap(&keymap_config);
}

TEST_F(EraBacklightTapHoldTiming, ADanceStartedAfterAClearInTheSameDispatchOwnsItsRelease) {
    TestDriver driver;
    unsigned a_downs = 0;
    bool     a_prev  = false;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        const bool a = report_has(report, KC_A);
        a_downs += a && !a_prev;
        a_prev = a;
    });
    auto lt = KeymapKey(0, 3, 0, LT(1, KC_D));
    auto t1 = KeymapKey(0, 1, 0, TD(1));
    auto t2 = KeymapKey(0, 2, 0, TD(2));
    set_keymap({lt, t1, t2, KeymapKey(1, 1, 0, KC_TRNS), KeymapKey(1, 2, 0, KC_TRNS), KeymapKey(1, 3, 0, KC_TRNS)});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, QK_MAGIC_TOGGLE_GUI));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_NO));
    ASSERT_TRUE(era_tapdance_set_action(2, 0, KC_A));
    ASSERT_TRUE(era_tapdance_set_action(2, 1, KC_LCTL));
    const bool no_gui = keymap_config.no_gui;
    /* Both dances wait behind the LT; TD2's press interrupts TD1, whose magic
       keycode clears the keyboard inside the same dispatch. */
    lt.press(); run_one_scan_loop(); idle_for(20);
    t1.press(); run_one_scan_loop(); idle_for(20);
    t2.press(); run_one_scan_loop(); idle_for(20);
    t2.release(); run_one_scan_loop();
    idle_for(TAPPING_TERM);
    lt.release(); run_one_scan_loop(); idle_for(50);
    t1.release(); run_one_scan_loop(); idle_for(TAPPING_TERM * 2);
    EXPECT_EQ(a_downs, 1U);
    EXPECT_EQ(get_mods(), 0U);
    EXPECT_EQ(keyboard_report->mods, 0U);
    EXPECT_EQ(tap_dance_get_state(1), nullptr);
    EXPECT_EQ(tap_dance_get_state(2), nullptr);
    keymap_config.no_gui = no_gui;
    eeconfig_update_keymap(&keymap_config);
}

TEST_F(EraBacklightTapHoldTiming, AClearFromADanceOutputRetiresAnotherDancesModifierAtOnce) {
    TestDriver driver;
    uint8_t x_mods = 0xFF;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        if (report_has(report, KC_X) && x_mods == 0xFF) x_mods = report.mods;
    });
    auto ctrl = KeymapKey(0, 2, 0, TD(2));
    auto td   = KeymapKey(0, 1, 0, TD(1));
    set_keymap({ctrl, td});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, QK_MAGIC_TOGGLE_GUI));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, KC_X));
    ASSERT_TRUE(era_tapdance_set_action(1, 2, KC_Z));
    ASSERT_TRUE(era_tapdance_set_action(1, 3, KC_NO));
    ASSERT_TRUE(era_tapdance_set_action(2, 0, KC_B));
    ASSERT_TRUE(era_tapdance_set_action(2, 1, KC_LCTL));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, TAPPING_TERM));
    const bool no_gui = keymap_config.no_gui;
    ctrl.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    ASSERT_EQ(get_mods(), MOD_BIT(KC_LCTL));
    /* The fallback's magic tap clears the keyboard: the other dance's Ctrl is
       retired before this dance's hold, so the host never sees Ctrl+X. */
    td.press(); run_one_scan_loop(); td.release(); run_one_scan_loop();
    td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    EXPECT_EQ(x_mods, 0U);
    EXPECT_TRUE(report_has(*keyboard_report, KC_X));
    td.release(); run_one_scan_loop(); ctrl.release(); run_one_scan_loop(); idle_for(TAPPING_TERM * 2);
    EXPECT_EQ(get_mods(), 0U);
    EXPECT_FALSE(report_has(*keyboard_report, KC_X));
    EXPECT_EQ(tap_dance_get_state(1), nullptr);
    EXPECT_EQ(tap_dance_get_state(2), nullptr);
    keymap_config.no_gui = no_gui;
    eeconfig_update_keymap(&keymap_config);
}

TEST_F(EraBacklightTapHoldTiming, APeerHalfPressCancelsADanceHoldsRetroTap) {
    TestDriver driver;
    unsigned c_downs = 0;
    bool     c_prev  = false;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).WillRepeatedly([&](report_keyboard_t &report) {
        const bool c = report_has(report, KC_C);
        c_downs += c && !c_prev;
        c_prev = c;
    });
    auto td = KeymapKey(0, 1, 0, TD(1));
    set_keymap({td});
    ASSERT_TRUE(era_tapdance_set_action(1, 0, KC_ESC));
    ASSERT_TRUE(era_tapdance_set_action(1, 1, MT(MOD_LCTL, KC_C)));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(1, TAPPING_TERM));
    era_tapping_set_retro_tapping(true);
    for (bool peer_press : {false, true}) {
        c_downs = 0;
        era_test_split_reset();
        td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
        if (peer_press) {
            /* DUAL-HOST: the other half's press arrives only as its activity
               record, never as a local event. */
            era_test_split_peer_press(timer_read32());
        }
        td.release(); run_one_scan_loop(); idle_for(10);
        EXPECT_EQ(c_downs, peer_press ? 0U : 1U) << "peer press " << peer_press;
        EXPECT_EQ(get_mods(), 0U);
        EXPECT_EQ(tap_dance_get_state(1), nullptr);
    }
    /* A newer dance hold takes the retro-pending window over, as a new engine
       key does, so it outlives the older hold's up; the older hold's retro was
       already cleared by the newer press. */
    auto second = KeymapKey(0, 2, 0, TD(2));
    set_keymap({td, second});
    ASSERT_TRUE(era_tapdance_set_action(2, 0, KC_ESC));
    ASSERT_TRUE(era_tapdance_set_action(2, 1, MT(MOD_LSFT, KC_C)));
    ASSERT_TRUE(era_tapdance_set_slot_term_ms_exact(2, TAPPING_TERM));
    c_downs = 0;
    era_test_split_reset();
    td.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    second.press(); run_one_scan_loop(); idle_for(TAPPING_TERM + 1);
    td.release(); run_one_scan_loop(); idle_for(10);
    era_test_split_peer_press(timer_read32());
    second.release(); run_one_scan_loop(); idle_for(10);
    EXPECT_EQ(c_downs, 0U);
    EXPECT_EQ(get_mods(), 0U);
    era_tapping_set_retro_tapping(false);
    era_test_split_reset();
}
