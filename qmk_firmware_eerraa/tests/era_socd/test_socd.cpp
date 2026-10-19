// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "keyboard_report_util.hpp"
#include "test_common.hpp"

extern "C" {
#include "keyboards/era/common/features/era_socd.h"
#include "keyboards/era/common/features/era_kkuk.h"
#include "usb_device_state.h"
#define _Static_assert static_assert
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#undef _Static_assert
}

using testing::_;
using testing::AnyNumber;
using testing::Invoke;

namespace {

std::array<uint8_t, ERA_EEPROM_CONFIG_SIZE> g_era_config{};

bool has_key(const report_keyboard_t &report, uint8_t code) {
    return std::find(std::begin(report.keys), std::end(report.keys), code) != std::end(report.keys);
}

bool has_key(const report_nkro_t &report, uint8_t code) {
    return (report.bits[code >> 3] & (1U << (code & 7))) != 0;
}

} // namespace

extern "C" {

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

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    return era_kkuk_process_record(keycode, record);
}

void housekeeping_task_user(void) {
    era_kkuk_task();
}

} // extern "C"

class Socd : public TestFixture {
   protected:
    std::vector<report_keyboard_t> sent;

    void SetUp() override {
        g_era_config.fill(0);
        era_socd_init();
        era_kkuk_init();
        sent.clear();
    }

    /* Every report the host receives, so a case can check that no single one
       ever carried both keys of a pair. Pairs are set after this: setting one
       sends a report. */
    void record(TestDriver &driver) {
        EXPECT_CALL(driver, send_keyboard_mock(_)).Times(AnyNumber()).WillRepeatedly(Invoke([this](report_keyboard_t &report) { sent.push_back(report); }));
    }

    static void set_pair(uint8_t pair, uint16_t key0, uint16_t key1, uint8_t mode) {
        ASSERT_TRUE(era_socd_set_keycode(pair, 0, key0));
        ASSERT_TRUE(era_socd_set_keycode(pair, 1, key1));
        ASSERT_TRUE(era_socd_set_mode(pair, mode));
        ASSERT_TRUE(era_socd_set_enabled(pair, true));
    }

    bool never_both(uint8_t a, uint8_t b) const {
        return std::none_of(sent.begin(), sent.end(), [&](const report_keyboard_t &report) { return has_key(report, a) && has_key(report, b); });
    }

    const report_keyboard_t &last() const {
        return sent.back();
    }
};

TEST_F(Socd, LastInputReportsTheNewerKeyAndRestoresTheHeldOne) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    set_keymap({a, d});
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_LAST_INPUT);

    a.press();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_A));
    d.press();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_D));
    EXPECT_FALSE(has_key(last(), KC_A));
    a.release();
    run_one_scan_loop();
    a.press();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_A)) << "a re-press is the newest input again";
    EXPECT_FALSE(has_key(last(), KC_D));
    a.release();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_D)) << "releasing the winner reports the key still held";
    d.release();
    run_one_scan_loop();
    EXPECT_FALSE(has_key(last(), KC_D));
    EXPECT_TRUE(never_both(KC_A, KC_D));
}

TEST_F(Socd, NeutralReportsNeitherWhileBothAreHeld) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    set_keymap({a, d});
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_NEUTRAL);

    a.press();
    run_one_scan_loop();
    d.press();
    run_one_scan_loop();
    EXPECT_FALSE(has_key(last(), KC_A));
    EXPECT_FALSE(has_key(last(), KC_D));
    a.release();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_D));
    d.release();
    run_one_scan_loop();
    EXPECT_TRUE(never_both(KC_A, KC_D));
}

TEST_F(Socd, FirstInputKeepsTheEarlierKey) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    set_keymap({a, d});
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_FIRST_INPUT);

    a.press();
    run_one_scan_loop();
    d.press();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_A));
    EXPECT_FALSE(has_key(last(), KC_D));
    a.release();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_D));
    d.release();
    run_one_scan_loop();
    EXPECT_TRUE(never_both(KC_A, KC_D));
}

TEST_F(Socd, PriorityModesIgnorePressOrder) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    set_keymap({a, d});
    for (uint8_t mode : {ERA_SOCD_MODE_KEY_0_PRIORITY, ERA_SOCD_MODE_KEY_1_PRIORITY}) {
        uint8_t winner = mode == ERA_SOCD_MODE_KEY_0_PRIORITY ? KC_A : KC_D;
        uint8_t loser  = winner == KC_A ? KC_D : KC_A;
        set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, mode);
        for (bool a_first : {false, true}) {
            auto &first  = a_first ? a : d;
            auto &second = a_first ? d : a;
            first.press();
            run_one_scan_loop();
            second.press();
            run_one_scan_loop();
            EXPECT_TRUE(has_key(last(), winner));
            EXPECT_FALSE(has_key(last(), loser));
            first.release();
            second.release();
            run_one_scan_loop();
        }
    }
    EXPECT_TRUE(never_both(KC_A, KC_D));
}

TEST_F(Socd, DisabledPairReportsBothKeys) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    set_keymap({a, d});
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_LAST_INPUT);
    ASSERT_TRUE(era_socd_set_enabled(ERA_SOCD_PAIR_LR, false));

    a.press();
    run_one_scan_loop();
    d.press();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_A) && has_key(last(), KC_D)) << "the watch below must be able to see both";
    a.release();
    d.release();
    run_one_scan_loop();
}

TEST_F(Socd, ChangingThePairWhileHeldReachesTheHostWithoutInput) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    set_keymap({a, d});
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_LAST_INPUT);

    a.press();
    run_one_scan_loop();
    d.press();
    run_one_scan_loop();
    ASSERT_FALSE(has_key(last(), KC_A));

    ASSERT_TRUE(era_socd_set_mode(ERA_SOCD_PAIR_LR, ERA_SOCD_MODE_NEUTRAL));
    EXPECT_FALSE(has_key(last(), KC_A));
    EXPECT_FALSE(has_key(last(), KC_D));

    ASSERT_TRUE(era_socd_set_enabled(ERA_SOCD_PAIR_LR, false));
    EXPECT_TRUE(has_key(last(), KC_A) && has_key(last(), KC_D)) << "the key the old setting hid comes back at once";

    size_t before = sent.size();
    ASSERT_TRUE(era_socd_set_enabled(ERA_SOCD_PAIR_LR, true));
    EXPECT_GT(sent.size(), before);
    EXPECT_FALSE(has_key(last(), KC_A) && has_key(last(), KC_D)) << "re-enabling applies to keys already held";
    a.release();
    d.release();
    run_one_scan_loop();
}

TEST_F(Socd, AmbiguousPairsStandDown) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    auto s = KeymapKey(0, 2, 0, KC_S);
    set_keymap({a, d, s});

    /* The same key on both sides is not a pair. */
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_A, ERA_SOCD_MODE_NEUTRAL);
    EXPECT_FALSE(era_socd_is_bound_keycode(KC_A));

    /* A key in both pairs leaves no single answer, so both pairs stand down. */
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_NEUTRAL);
    set_pair(ERA_SOCD_PAIR_UD, KC_A, KC_S, ERA_SOCD_MODE_NEUTRAL);
    EXPECT_FALSE(era_socd_is_bound_keycode(KC_D));
    a.press();
    d.press();
    s.press();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(last(), KC_A) && has_key(last(), KC_D) && has_key(last(), KC_S));
    a.release();
    d.release();
    s.release();
    run_one_scan_loop();
}

TEST_F(Socd, ModifierPairResolvesOnTheModifierByte) {
    TestDriver driver;
    record(driver);
    auto lctl = KeymapKey(0, 0, 0, KC_LCTL);
    auto lalt = KeymapKey(0, 1, 0, KC_LALT);
    set_keymap({lctl, lalt});
    set_pair(ERA_SOCD_PAIR_LR, KC_LCTL, KC_LALT, ERA_SOCD_MODE_LAST_INPUT);

    lctl.press();
    run_one_scan_loop();
    lalt.press();
    run_one_scan_loop();
    EXPECT_EQ(last().mods, MOD_BIT(KC_LALT));
    lalt.release();
    run_one_scan_loop();
    EXPECT_EQ(last().mods, MOD_BIT(KC_LCTL));
    lctl.release();
    run_one_scan_loop();
    for (const auto &report : sent) {
        EXPECT_NE(report.mods & (MOD_BIT(KC_LCTL) | MOD_BIT(KC_LALT)), MOD_BIT(KC_LCTL) | MOD_BIT(KC_LALT));
    }
}

TEST_F(Socd, NkroReportResolvesLikeTheSixKeyReport) {
    TestDriver                 driver;
    std::vector<report_nkro_t> nkro;
    EXPECT_CALL(driver, send_keyboard_mock(_)).Times(AnyNumber());
    EXPECT_CALL(driver, send_nkro_mock(_)).Times(AnyNumber()).WillRepeatedly(Invoke([&](report_nkro_t &report) { nkro.push_back(report); }));
    const auto old_protocol = usb_device_state_get_protocol();
    const bool old_nkro     = keymap_config.nkro;
    usb_device_state_set_protocol(USB_PROTOCOL_REPORT);
    keymap_config.nkro = true;

    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    set_keymap({a, d});
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_LAST_INPUT);
    a.press();
    run_one_scan_loop();
    d.press();
    run_one_scan_loop();
    ASSERT_FALSE(nkro.empty());
    EXPECT_TRUE(has_key(nkro.back(), KC_D));
    EXPECT_FALSE(has_key(nkro.back(), KC_A));
    d.release();
    run_one_scan_loop();
    EXPECT_TRUE(has_key(nkro.back(), KC_A));
    a.release();
    run_one_scan_loop();
    EXPECT_TRUE(std::none_of(nkro.begin(), nkro.end(), [](const report_nkro_t &report) { return has_key(report, KC_A) && has_key(report, KC_D); }));

    keymap_config.nkro = old_nkro;
    usb_device_state_set_protocol(old_protocol);
}

TEST_F(Socd, UnknownModeKeepsTheSettingAndStoredModesLoad) {
    TestDriver driver;
    record(driver);
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_FIRST_INPUT);
    EXPECT_TRUE(era_socd_set_mode(ERA_SOCD_PAIR_LR, ERA_SOCD_MODE_MAX + 1));
    EXPECT_EQ(era_socd_get_mode(ERA_SOCD_PAIR_LR), ERA_SOCD_MODE_FIRST_INPUT);

    era_socd_save_pair(ERA_SOCD_PAIR_LR);
    g_era_config[ERA_EEPROM_SOCD_LR_CONFIG_OFFSET + 1] = ERA_SOCD_MODE_NEUTRAL;
    era_socd_reload_from_eeprom();
    EXPECT_EQ(era_socd_get_mode(ERA_SOCD_PAIR_LR), ERA_SOCD_MODE_NEUTRAL);

    g_era_config[ERA_EEPROM_SOCD_LR_CONFIG_OFFSET + 1] = ERA_SOCD_MODE_MAX + 1;
    era_socd_reload_from_eeprom();
    EXPECT_EQ(era_socd_get_mode(ERA_SOCD_PAIR_LR), ERA_SOCD_MODE_LAST_INPUT) << "an unknown stored mode resets the pair";
    EXPECT_FALSE(era_socd_get_enabled(ERA_SOCD_PAIR_LR));
}

// KKUK leaves SOCD keys out of its count. A key counted at its press must be
// uncounted at its release even if SOCD claimed it in between, or one held key
// later reads as two and KKUK repeats.
TEST_F(Socd, KkukCountSurvivesASocdChangeWhileAKeyIsHeld) {
    TestDriver driver;
    record(driver);
    era_kkuk_set_enabled(true);
    auto b = KeymapKey(0, 0, 0, KC_B);
    auto c = KeymapKey(0, 1, 0, KC_C);
    auto d = KeymapKey(0, 2, 0, KC_D);
    set_keymap({b, c, d});

    b.press();
    run_one_scan_loop();
    set_pair(ERA_SOCD_PAIR_LR, KC_B, KC_D, ERA_SOCD_MODE_LAST_INPUT);
    b.release();
    run_one_scan_loop();

    c.press();
    run_one_scan_loop();
    size_t before = sent.size();
    idle_for(600);
    EXPECT_EQ(sent.size(), before) << "one held key must not start KKUK's empty pulses";
    c.release();
    run_one_scan_loop();
    era_kkuk_set_enabled(false);
}


// Ordinary B/C drive KKUK; SOCD's A/D winner must remain continuously down
// in every outgoing report, including the pulse that releases B/C.
TEST_F(Socd, KkukPreservesSocdAcrossAllModesAndBothReportFormats) {
    TestDriver driver;
    record(driver);
    std::vector<report_nkro_t> nkro;
    EXPECT_CALL(driver, send_nkro_mock(_)).Times(AnyNumber()).WillRepeatedly(Invoke([&](report_nkro_t &report) { nkro.push_back(report); }));
    const auto old_protocol = usb_device_state_get_protocol();
    const bool old_nkro = keymap_config.nkro;
    usb_device_state_set_protocol(USB_PROTOCOL_REPORT);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto d = KeymapKey(0, 1, 0, KC_D);
    auto b = KeymapKey(0, 2, 0, KC_B);
    auto c = KeymapKey(0, 3, 0, KC_C);
    set_keymap({a, d, b, c});
    for (bool bitmap : {false, true}) {
        keymap_config.nkro = bitmap;
        for (uint8_t mode = ERA_SOCD_MODE_LAST_INPUT; mode <= ERA_SOCD_MODE_MAX; mode++) {
            SCOPED_TRACE(testing::Message() << "nkro=" << bitmap << " mode=" << unsigned(mode));
            set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, mode);
            set_pair(ERA_SOCD_PAIR_UD, KC_F, KC_RIGHT, ERA_SOCD_MODE_LAST_INPUT);
            era_kkuk_set_delay_ticks(5);
            era_kkuk_set_repeat_ticks(5);
            era_kkuk_set_enabled(true);
            register_code(KC_F);
            a.press(); run_one_scan_loop();
            d.press(); run_one_scan_loop();
            b.press(); run_one_scan_loop();
            c.press(); run_one_scan_loop();
            const size_t before = bitmap ? nkro.size() : sent.size();
            idle_for(120);
            const size_t after = bitmap ? nkro.size() : sent.size();
            EXPECT_GT(after, before);
            const uint8_t winner = mode == ERA_SOCD_MODE_NEUTRAL ? KC_NO :
                mode == ERA_SOCD_MODE_FIRST_INPUT || mode == ERA_SOCD_MODE_KEY_0_PRIORITY ? KC_A : KC_D;
            bool saw_release = false, saw_restore = false;
            for (size_t i = before; i < after; i++) {
                auto has = [&](uint8_t key) { return bitmap ? has_key(nkro[i], key) : has_key(sent[i], key); };
                EXPECT_TRUE(has(KC_F));
                EXPECT_EQ(has(KC_A), winner == KC_A);
                EXPECT_EQ(has(KC_D), winner == KC_D);
                saw_release |= !has(KC_B) && !has(KC_C);
                saw_restore |= has(KC_B) && has(KC_C);
            }
            EXPECT_TRUE(saw_release);
            EXPECT_TRUE(saw_restore);
            era_kkuk_set_enabled(false);
            unregister_code(KC_F);
            a.release(); d.release(); b.release(); c.release(); run_one_scan_loop();
        }
    }
    keymap_config.nkro = old_nkro;
    usb_device_state_set_protocol(old_protocol);
}

TEST_F(Socd, KkukDoesNotProtectDisabledOrAmbiguousPairs) {
    TestDriver driver;
    record(driver);
    auto a = KeymapKey(0, 0, 0, KC_A);
    auto b = KeymapKey(0, 1, 0, KC_B);
    auto c = KeymapKey(0, 2, 0, KC_C);
    set_keymap({a, b, c});
    for (bool overlap : {false, true}) {
        set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_LAST_INPUT);
        if (overlap) set_pair(ERA_SOCD_PAIR_UD, KC_A, KC_S, ERA_SOCD_MODE_LAST_INPUT);
        else ASSERT_TRUE(era_socd_set_enabled(ERA_SOCD_PAIR_LR, false));
        era_kkuk_set_delay_ticks(5); era_kkuk_set_repeat_ticks(5); era_kkuk_set_enabled(true);
        a.press(); b.press(); c.press(); run_one_scan_loop();
        const size_t before = sent.size();
        idle_for(120);
        EXPECT_TRUE(std::any_of(sent.begin() + before, sent.end(), [](const report_keyboard_t &report) {
            return !has_key(report, KC_A) && !has_key(report, KC_B) && !has_key(report, KC_C);
        }));
        era_kkuk_set_enabled(false);
        a.release(); b.release(); c.release(); run_one_scan_loop();
    }
}

// A synthetic report usage (as from TD/macro) and its real release between
// pulse ticks must not be replaced with the earlier snapshot on restore.
TEST_F(Socd, KkukRestoresTheCurrentReportAfterAProtectedUsageIsReleased) {
    TestDriver driver;
    record(driver);
    auto b = KeymapKey(0, 0, 0, KC_B);
    auto c = KeymapKey(0, 1, 0, KC_C);
    set_keymap({b, c});
    set_pair(ERA_SOCD_PAIR_LR, KC_A, KC_D, ERA_SOCD_MODE_LAST_INPUT);
    era_kkuk_set_delay_ticks(5); era_kkuk_set_repeat_ticks(5); era_kkuk_set_enabled(true);
    register_code(KC_A);
    register_code(KC_LCTL);
    b.press(); c.press(); run_one_scan_loop();
    bool pulsed = false;
    for (unsigned ms = 0; ms < 150; ms++) {
        run_one_scan_loop();
        if (!has_key(last(), KC_B) && !has_key(last(), KC_C)) { pulsed = true; break; }
    }
    EXPECT_TRUE(pulsed);
    EXPECT_TRUE(has_key(last(), KC_A));
    EXPECT_EQ(last().mods, MOD_BIT(KC_LCTL));
    unregister_code(KC_A);
    era_kkuk_task();
    EXPECT_FALSE(has_key(last(), KC_A));
    EXPECT_TRUE(has_key(last(), KC_B) && has_key(last(), KC_C));
    EXPECT_EQ(last().mods, MOD_BIT(KC_LCTL));
    era_kkuk_set_enabled(false);
    unregister_code(KC_LCTL);
    b.release(); c.release(); run_one_scan_loop();
}
