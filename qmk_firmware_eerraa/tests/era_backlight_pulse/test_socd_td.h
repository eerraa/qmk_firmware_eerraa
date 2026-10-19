// SOCD resolves on the outgoing report, so a pair holds whoever put each usage
// there: a Tap Dance output counts like a physical key. Each case records every
// report the host received and checks that none carried both keys of the pair.

extern "C" {
#include "keyboards/era/common/features/era_socd.h"
#include "usb_device_state.h"
}

namespace {

bool socd_report_has(const report_keyboard_t &report, uint8_t code) {
    return std::find(std::begin(report.keys), std::end(report.keys), code) != std::end(report.keys);
}

void socd_set_pair(uint16_t key0, uint16_t key1, bool enabled) {
    era_socd_init();
    ASSERT_TRUE(era_socd_set_keycode(ERA_SOCD_PAIR_LR, 0, key0));
    ASSERT_TRUE(era_socd_set_keycode(ERA_SOCD_PAIR_LR, 1, key1));
    ASSERT_TRUE(era_socd_set_mode(ERA_SOCD_PAIR_LR, ERA_SOCD_MODE_LAST_INPUT));
    ASSERT_TRUE(era_socd_set_enabled(ERA_SOCD_PAIR_LR, enabled));
}

} // namespace

TEST_F(EraBacklightTapHoldTiming, SocdResolvesADanceHoldAgainstThePhysicalOpposingKey) {
    TestDriver                     driver;
    std::vector<report_keyboard_t> sent;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber()).WillRepeatedly(testing::Invoke([&](report_keyboard_t &report) { sent.push_back(report); }));
    auto both = [&] { return std::any_of(sent.begin(), sent.end(), [](const report_keyboard_t &report) { return socd_report_has(report, KC_A) && socd_report_has(report, KC_D); }); };

    for (bool enabled : {false, true}) for (bool dance_first : {false, true}) {
        auto td = KeymapKey(0, 1, 0, TD(0));
        auto d  = KeymapKey(0, 2, 0, KC_D);
        set_keymap({td, d});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_A));
        socd_set_pair(KC_A, KC_D, enabled);
        sent.clear();

        if (dance_first) {
            td.press(); run_one_scan_loop(); idle_for(201);
            d.press(); run_one_scan_loop();
        } else {
            d.press(); run_one_scan_loop();
            td.press(); run_one_scan_loop(); idle_for(201);
        }
        const uint8_t newer = dance_first ? KC_D : KC_A;
        const uint8_t older = dance_first ? KC_A : KC_D;
        EXPECT_TRUE(socd_report_has(sent.back(), newer));
        EXPECT_EQ(socd_report_has(sent.back(), older), !enabled);
        if (dance_first) { d.release(); } else { td.release(); }
        run_one_scan_loop();
        EXPECT_TRUE(socd_report_has(sent.back(), older)) << "the held key returns when the newer one is released";
        if (dance_first) { td.release(); } else { d.release(); }
        run_one_scan_loop();
        EXPECT_FALSE(socd_report_has(sent.back(), KC_A) || socd_report_has(sent.back(), KC_D));
        EXPECT_EQ(both(), !enabled) << (enabled ? "SOCD must hold a dance output too" : "the watch must see both when SOCD is off");
    }
    era_socd_init();
}

TEST_F(EraBacklightTapHoldTiming, SocdResolvesADanceTapAndASharedUsage) {
    TestDriver                     driver;
    std::vector<report_keyboard_t> sent;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber()).WillRepeatedly(testing::Invoke([&](report_keyboard_t &report) { sent.push_back(report); }));

    /* A dance tap of A while D is held. */
    {
        auto td = KeymapKey(0, 1, 0, TD(0));
        auto d  = KeymapKey(0, 2, 0, KC_D);
        set_keymap({td, d});
        ASSERT_TRUE(era_tapdance_set_action(0, 0, KC_A));
        socd_set_pair(KC_A, KC_D, true);
        sent.clear();
        d.press(); run_one_scan_loop();
        td.press(); run_one_scan_loop();
        td.release(); run_one_scan_loop();
        idle_for(250);
        EXPECT_TRUE(std::any_of(sent.begin(), sent.end(), [](const report_keyboard_t &report) { return socd_report_has(report, KC_A); })) << "the tap reached the host";
        EXPECT_TRUE(socd_report_has(sent.back(), KC_D));
        d.release(); run_one_scan_loop();
        EXPECT_TRUE(std::none_of(sent.begin(), sent.end(), [](const report_keyboard_t &report) { return socd_report_has(report, KC_A) && socd_report_has(report, KC_D); }));
    }
    /* A held by a physical key and a dance at once still yields to D, and comes back after it. */
    {
        auto td = KeymapKey(0, 1, 0, TD(0));
        auto a  = KeymapKey(0, 2, 0, KC_A);
        auto d  = KeymapKey(0, 3, 0, KC_D);
        set_keymap({td, a, d});
        ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_A));
        socd_set_pair(KC_A, KC_D, true);
        sent.clear();
        a.press(); run_one_scan_loop();
        td.press(); run_one_scan_loop(); idle_for(201);
        d.press(); run_one_scan_loop();
        EXPECT_TRUE(socd_report_has(sent.back(), KC_D));
        EXPECT_FALSE(socd_report_has(sent.back(), KC_A));
        d.release(); run_one_scan_loop();
        EXPECT_TRUE(socd_report_has(sent.back(), KC_A));
        a.release(); run_one_scan_loop();
        EXPECT_TRUE(socd_report_has(sent.back(), KC_A)) << "the dance still holds A";
        td.release(); run_one_scan_loop();
        EXPECT_TRUE(std::none_of(sent.begin(), sent.end(), [](const report_keyboard_t &report) { return socd_report_has(report, KC_A) && socd_report_has(report, KC_D); }));
    }
    era_socd_init();
}

TEST_F(EraBacklightTapHoldTiming, SocdResolvesADanceHoldInTheNkroReport) {
    TestDriver                 driver;
    std::vector<report_nkro_t> sent;
    EXPECT_CALL(driver, send_keyboard_mock(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(driver, send_nkro_mock(testing::_)).Times(testing::AnyNumber()).WillRepeatedly(testing::Invoke([&](report_nkro_t &report) { sent.push_back(report); }));
    auto has = [](const report_nkro_t &report, uint8_t code) { return (report.bits[code >> 3] & (1U << (code & 7))) != 0; };
    const auto old_protocol = usb_device_state_get_protocol();
    const bool old_nkro     = keymap_config.nkro;
    usb_device_state_set_protocol(USB_PROTOCOL_REPORT);
    keymap_config.nkro = true;

    auto td = KeymapKey(0, 1, 0, TD(0));
    auto d  = KeymapKey(0, 2, 0, KC_D);
    set_keymap({td, d});
    ASSERT_TRUE(era_tapdance_set_action(0, 1, KC_A));
    socd_set_pair(KC_A, KC_D, true);
    sent.clear();
    d.press(); run_one_scan_loop();
    td.press(); run_one_scan_loop(); idle_for(201);
    ASSERT_FALSE(sent.empty());
    EXPECT_TRUE(has(sent.back(), KC_A));
    EXPECT_FALSE(has(sent.back(), KC_D));
    td.release(); run_one_scan_loop();
    EXPECT_TRUE(has(sent.back(), KC_D));
    d.release(); run_one_scan_loop();
    EXPECT_TRUE(std::none_of(sent.begin(), sent.end(), [&](const report_nkro_t &report) { return has(report, KC_A) && has(report, KC_D); }));

    keymap_config.nkro = old_nkro;
    usb_device_state_set_protocol(old_protocol);
    era_socd_init();
}
