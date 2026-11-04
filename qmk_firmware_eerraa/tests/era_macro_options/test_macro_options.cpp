// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <vector>
#include "gtest/gtest.h"
#include "test_common.hpp"
#include "test_fixture.hpp"
#include "test_keymap_key.hpp"
extern "C" {
#include "era_macro.h"
#include "dynamic_keymap.h"
#include "eeconfig.h"
#include "eeprom.h"
#include "send_string.h"
#include "via.h"
#include "keyboards/era/common/features/era_socd.h"
#include "keyboards/era/common/features/era_tapdance.h"
#include "keyboards/era/common/system/era_common_via.h"
#include "keyboards/era/common/system/era_macro_options.h"
#include "keyboards/era/common/system/era_state_sync.h"
#include "keyboards/era/common/system/era_wire.h"
#define _Static_assert static_assert
#include "keyboards/era/common/storage/era_storage_layout.h"
#undef _Static_assert
void raw_hid_receive(uint8_t *data, uint8_t length);
void eeprom_reject_write(void);
void __real_eeprom_write_block(const void *buf, void *addr, size_t len);
}
using testing::_;

#define M_DOWN(code) SS_QMK_PREFIX, SS_DOWN_CODE, (code)
#define M_UP(code) SS_QMK_PREFIX, SS_UP_CODE, (code)
#define M_TAP(code) SS_QMK_PREFIX, SS_TAP_CODE, (code)
#define M_DELAY(...) SS_QMK_PREFIX, SS_DELAY_CODE, __VA_ARGS__, '|'

namespace {
constexpr uint8_t   kChannel    = ERA_WIRE_CHANNELS_EERRAA_MACRO_OPTIONS;
constexpr uint8_t   kRetap      = ERA_WIRE_MACRO_OPTIONS_FLAGS_RETAP;
constexpr uint8_t   kHold       = ERA_WIRE_MACRO_OPTIONS_FLAGS_HOLD;
constexpr uint8_t   kRelease    = ERA_WIRE_MACRO_OPTIONS_FLAGS_RELEASE;
constexpr uint8_t   kOther      = ERA_WIRE_MACRO_OPTIONS_FLAGS_OTHER;
constexpr uint8_t   kRepeat     = ERA_WIRE_MACRO_OPTIONS_FLAGS_REPEAT;
constexpr uint8_t   kIgnoreMods = ERA_WIRE_MACRO_OPTIONS_FLAGS_IGNORE_MODS;
constexpr uint8_t   kCount      = DYNAMIC_KEYMAP_MACRO_COUNT;
constexpr uintptr_t kTable      = ERA_STORAGE_MACRO_OPTIONS_ADDR;
constexpr size_t    kTableBytes = ERA_STORAGE_MACRO_OPTIONS_BYTES;
using Frame                     = std::array<uint8_t, 32>;

uint32_t session = 1;
bool     connected = true, sleeping, pressure, fail_writes;
unsigned replies, table_writes;
Frame    reply;
} // namespace

extern "C" {
void era_macro_platform_sync(void) {
    era_macro_session(session, connected, sleeping);
}
bool era_macro_platform_pressured(void) {
    return pressure;
}
void host_keyboard_delay(uint16_t) {} // The report transport has its own suites.
void raw_hid_send(uint8_t *data, uint8_t length) {
    ++replies;
    reply.fill(0);
    std::copy_n(data, std::min<size_t>(length, reply.size()), reply.begin());
}
void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (!era_common_via_handle_command(data, length)) data[0] = id_unhandled;
}
/* Like the production adapter (storage/era_eeprom_driver.c), classify each
 * accepted write for State Sync; a rejected write fails its transaction. */
void __wrap_eeprom_write_block(const void *buf, void *addr, size_t len) {
    const uintptr_t at = (uintptr_t)addr;
    if (at < kTable + kTableBytes && at + len > kTable) ++table_writes;
    if (fail_writes) {
        eeprom_reject_write();
        return;
    }
    __real_eeprom_write_block(buf, addr, len);
    era_state_sync_note_eeprom_mask(era_state_sync_eeprom_span_mask((uint16_t)at, (uint16_t)len));
}
}

class EraMacroOptions : public TestFixture {
   protected:
    testing::NiceMock<TestDriver> driver;
    std::vector<report_keyboard_t> reports;
    KeymapKey                      m0{0, 0, 0, QK_MACRO + 0};
    KeymapKey                      m0_twin{0, 1, 0, QK_MACRO + 0};
    KeymapKey                      m1{0, 2, 0, QK_MACRO + 1};
    KeymapKey                      m2{0, 3, 0, QK_MACRO + 2};
    KeymapKey                      a{0, 4, 0, KC_A};
    KeymapKey                      shift{0, 5, 0, KC_LSFT};
    KeymapKey                      mod_tap{0, 6, 0, LSFT_T(KC_B)};
    KeymapKey                      dance{0, 7, 0, ERA_TAP_DANCE_KEYCODE_BASE + 0};
    KeymapKey                      d{0, 8, 0, KC_D};
    std::array<KeymapKey, 5>       twins{{{0, 0, 1, QK_MACRO + 0}, {0, 1, 1, QK_MACRO + 0}, {0, 2, 1, QK_MACRO + 0}, {0, 3, 1, QK_MACRO + 0}, {0, 4, 1, QK_MACRO + 0}}};

    void SetUp() override {
        ON_CALL(driver, send_keyboard_mock(_)).WillByDefault([this](report_keyboard_t &r) { reports.push_back(r); });
        connected = true;
        sleeping = pressure = fail_writes = false;
        ++session;
        era_macro_platform_sync();
        eeconfig_init(); // An EEPROM clear: empty macros and default options.
        era_macro_options_init();
        era_tapdance_init(); // Every dance action empty, SOCD off.
        era_socd_init();
        set_keymap({m0, m0_twin, m1, m2, a, shift, mod_tap, dance, d, twins[0], twins[1], twins[2], twins[3], twins[4]});
        reports.clear();
        table_writes = 0;
    }
    void TearDown() override {
        era_macro_cancel();
        clear_keyboard();
    }

    /* VIA's upload transcript: a nonzero final marker opens it, the zero closes it. */
    void macros(std::initializer_list<std::initializer_list<uint8_t>> list) {
        std::vector<uint8_t> buffer;
        for (auto macro : list) {
            buffer.insert(buffer.end(), macro.begin(), macro.end());
            buffer.push_back(0);
        }
        const uint16_t marker = dynamic_keymap_macro_get_buffer_size() - 1U;
        uint8_t        open = 0xFF, close = 0;
        dynamic_keymap_macro_set_buffer(marker, 1, &open);
        dynamic_keymap_macro_set_buffer(0, buffer.size(), buffer.data());
        dynamic_keymap_macro_set_buffer(marker, 1, &close);
        ASSERT_TRUE(eeprom_persistence_ok());
    }
    void run(unsigned ms = 1) {
        while (ms--) {
            run_one_scan_loop();
            era_macro_task();
        }
    }
    void press(KeymapKey &key) {
        key.press();
        run();
    }
    void release(KeymapKey &key) {
        key.release();
        run();
    }
    void tap(KeymapKey &key) {
        press(key);
        release(key);
    }
    static bool has(const report_keyboard_t &report, uint8_t code) {
        return std::find(std::begin(report.keys), std::end(report.keys), code) != std::end(report.keys);
    }
    unsigned presses(uint8_t code) const {
        unsigned count = 0;
        bool     down  = false;
        for (const auto &report : reports) {
            const bool now = has(report, code);
            count += now && !down;
            down = now;
        }
        return count;
    }
    uint8_t report_mods() const {
        EXPECT_FALSE(reports.empty());
        return reports.empty() ? 0xFF : reports.back().mods;
    }
    bool held(uint8_t code) {
        return is_key_pressed(code);
    }

    Frame via(std::initializer_list<uint8_t> bytes, uint8_t length = 32) {
        Frame frame{};
        std::copy(bytes.begin(), bytes.end(), frame.begin());
        replies = 0;
        raw_hid_receive(frame.data(), length);
        EXPECT_EQ(replies, 1U);
        return reply;
    }
    Frame set_flags(uint8_t index, uint8_t flags) {
        return via({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_FLAGS, index, flags});
    }
    Frame set_hold(uint8_t index, uint16_t ms) {
        return via({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_HOLD_MS, index, uint8_t(ms >> 8), uint8_t(ms)});
    }
    uint8_t flags_of(uint8_t index) {
        const Frame r = via({id_custom_get_value, kChannel, ERA_WIRE_MACRO_OPTIONS_FLAGS, index});
        EXPECT_EQ(r[0], id_custom_get_value);
        return r[4];
    }
    uint16_t hold_of(uint8_t index) {
        const Frame r = via({id_custom_get_value, kChannel, ERA_WIRE_MACRO_OPTIONS_HOLD_MS, index});
        EXPECT_EQ(r[0], id_custom_get_value);
        return uint16_t(r[4] << 8 | r[5]);
    }
    Frame save() {
        return via({id_custom_save, kChannel});
    }
};

TEST_F(EraMacroOptions, MacroKeyDuringPlaybackNeitherStartsNorQueues) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0'), M_UP(KC_X)}, {M_TAP(KC_Y)}});
    tap(m0);
    EXPECT_TRUE(held(KC_X));
    tap(m1);
    run(300);
    EXPECT_FALSE(held(KC_X));
    EXPECT_EQ(presses(KC_Y), 0U);
    tap(m1); // Outside playback the same key starts its macro.
    run(5);
    EXPECT_EQ(presses(KC_Y), 1U);
}

TEST_F(EraMacroOptions, PressBegunDuringPlaybackNeverStartsAfterPlaybackEnds) {
    macros({{M_DOWN(KC_X), M_DELAY('5', '0'), M_UP(KC_X)}, {M_TAP(KC_Y)}});
    tap(m0);
    EXPECT_TRUE(held(KC_X));
    press(mod_tap); // Undecided, so the next press waits in the tapping buffer.
    press(m1);
    run(100);
    EXPECT_FALSE(held(KC_X)); // Playback ended while that press waited.
    run(TAPPING_TERM);
    release(m1);
    release(mod_tap);
    run(5);
    EXPECT_EQ(presses(KC_Y), 0U);
}

TEST_F(EraMacroOptions, TapDanceBegunDuringPlaybackNeverStartsItsMacro) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0'), M_UP(KC_X)}, {M_TAP(KC_Y)}});
    ASSERT_TRUE(era_tapdance_set_action(0, 0, QK_MACRO + 1)); // A tap plays macro 1, decided at its release.
    tap(m0);
    ASSERT_TRUE(held(KC_X));
    press(dance); // Begun during playback ...
    run(150);
    ASSERT_FALSE(held(KC_X));
    release(dance); // ... and decided after it ended.
    run(5);
    EXPECT_EQ(presses(KC_Y), 0U);

    set_flags(0, kOther); // A dance key is another key: it stops the playback, and still starts nothing.
    tap(m0);
    ASSERT_TRUE(held(KC_X));
    press(dance);
    EXPECT_FALSE(held(KC_X));
    release(dance);
    run(5);
    EXPECT_EQ(presses(KC_Y), 0U);

    tap(dance); // Outside playback the dance starts its macro.
    run(5);
    EXPECT_EQ(presses(KC_Y), 1U);
}

TEST_F(EraMacroOptions, StopOnReleaseEndsAtTheStartingKeysRelease) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0', '0'), M_UP(KC_X)}});
    tap(m0); // Option off: today's behavior.
    run(100);
    EXPECT_TRUE(held(KC_X));
    run(1000);
    EXPECT_FALSE(held(KC_X));

    set_flags(0, kRelease);
    press(m0);
    run(100);
    EXPECT_TRUE(held(KC_X));
    release(m0);
    EXPECT_FALSE(held(KC_X));
    run(1200);
    EXPECT_EQ(presses(KC_X), 2U); // The stop never restarts the macro.

    set_flags(0, kRelease | kRetap | kHold); // One release rule decides.
    set_hold(0, 50);
    press(m0);
    press(m0_twin);
    run(100);
    release(m0_twin);
    EXPECT_TRUE(held(KC_X));
    release(m0);
    EXPECT_FALSE(held(KC_X));
}

TEST_F(EraMacroOptions, StopOnReleaseJudgesAStartingReleaseQueuedBehindATapHoldKey) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0'), M_UP(KC_X)}});
    for (uint8_t flags : {uint8_t(0), kRelease}) {
        set_flags(0, flags);
        press(mod_tap);
        tap(m0); // Its press and release wait behind the undecided key.
        release(mod_tap);
        run(200);
    }
    EXPECT_EQ(presses(KC_B), 2U);
    EXPECT_EQ(presses(KC_X), 1U); // Only the run with the option off played.
}

TEST_F(EraMacroOptions, StopOnReleaseWaitsForTheDancesPhysicalRelease) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0', '0'), M_UP(KC_X)}});
    set_flags(0, kRelease);
    /* A tap plays macro 0; a tap-hold action keeps the first two taps
     * undecided, so the third press taps macro 0 three times, each with a
     * synthesized release, while the key stays down. */
    ASSERT_TRUE(era_tapdance_set_action(0, 0, QK_MACRO + 0));
    ASSERT_TRUE(era_tapdance_set_action(0, 3, KC_B));
    tap(dance);
    tap(dance);
    press(dance);
    run(100);
    EXPECT_TRUE(held(KC_X));
    release(dance);
    EXPECT_FALSE(held(KC_X));
    run(1100);
    EXPECT_EQ(presses(KC_X), 1U);
}

TEST_F(EraMacroOptions, StopOnRetapEndsOnTheReleaseOfANewPressOfTheMacrosKey) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0', '0'), M_UP(KC_X)}});
    set_flags(0, kRetap);
    tap(m0);
    run(50);
    EXPECT_TRUE(held(KC_X)); // The starting press never counts.
    press(m0);
    run(50);
    EXPECT_TRUE(held(KC_X));
    release(m0);
    EXPECT_FALSE(held(KC_X));

    tap(m0);
    tap(m0_twin); // Any key bound to the playing macro.
    EXPECT_FALSE(held(KC_X));

    set_flags(0, kRetap | kHold);
    set_hold(0, 300);
    tap(m0);
    press(m0_twin);
    run(100);
    EXPECT_TRUE(held(KC_X));
    release(m0_twin); // Before the hold time.
    EXPECT_FALSE(held(KC_X));

    set_flags(0, 0);
    tap(m0);
    tap(m0_twin);
    EXPECT_TRUE(held(KC_X));
    run(1100);
    EXPECT_FALSE(held(KC_X));
    EXPECT_EQ(presses(KC_X), 4U);
}

TEST_F(EraMacroOptions, StopOnHoldEndsAtTheHoldTimeEvenDuringADelay) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0', '0'), M_UP(KC_X)}});
    set_flags(0, kHold);
    set_hold(0, 200);
    tap(m0);
    press(m0_twin);
    run(198);
    EXPECT_TRUE(held(KC_X));
    run(1);
    EXPECT_FALSE(held(KC_X));
    release(m0_twin);
    run(1200);
    EXPECT_EQ(presses(KC_X), 1U); // The stop never restarts the macro.

    tap(m0);
    press(m0_twin);
    run(100);
    release(m0_twin); // Before the hold time, and re-tap is off.
    run(200);
    EXPECT_TRUE(held(KC_X));
    run(800);
    EXPECT_FALSE(held(KC_X));

    set_flags(0, kRetap | kHold); // The hold time comes first.
    tap(m0);
    press(m0_twin);
    run(199);
    EXPECT_FALSE(held(KC_X));
    release(m0_twin);
    EXPECT_EQ(presses(KC_X), 3U);
}

TEST_F(EraMacroOptions, EveryPressOfTheMacrosKeysIsJudged) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0', '0'), M_UP(KC_X)}});
    set_flags(0, kRetap);
    tap(m0);
    for (auto &twin : twins) press(twin);
    release(twins[4]); // The fifth press begun during playback counts as well.
    EXPECT_FALSE(held(KC_X));
    for (uint8_t i = 0; i < 4; ++i) release(twins[i]);
    run(1100);

    set_flags(0, kHold);
    set_hold(0, 300);
    tap(m0);
    for (auto &twin : twins) {
        press(twin);
        run(9);
    }
    for (uint8_t i = 0; i < 4; ++i) release(twins[i]); // Each before its hold time.
    run(260); // Past the first press's hold time.
    EXPECT_TRUE(held(KC_X));
    run(40); // Past the fifth's.
    EXPECT_FALSE(held(KC_X));
    release(twins[4]);
    EXPECT_EQ(presses(KC_X), 2U);
}

TEST_F(EraMacroOptions, StopOnOtherKeyStopsBeforeThatKeysActionAndStartsNoMacro) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0', '0'), M_UP(KC_X)}, {M_TAP(KC_Y)}});
    tap(m0);
    tap(a);
    EXPECT_TRUE(held(KC_X)); // Option off: another key leaves playback running.
    run(1100);

    set_flags(0, kOther);
    tap(m0);
    tap(m0_twin); // A key bound to the playing macro is no other key.
    EXPECT_TRUE(held(KC_X));
    press(a);
    EXPECT_FALSE(held(KC_X));
    EXPECT_TRUE(held(KC_A)); // That key's own action still happens.
    release(a);

    tap(m0);
    press(shift); // Modifiers count.
    EXPECT_FALSE(held(KC_X));
    EXPECT_TRUE(get_mods() & MOD_BIT(KC_LSFT));
    release(shift);

    tap(m0);
    tap(m1); // Another macro key stops it and does not start its own macro.
    EXPECT_FALSE(held(KC_X));
    run(1100);
    EXPECT_EQ(presses(KC_Y), 0U);
    EXPECT_EQ(presses(KC_X), 4U);
}

TEST_F(EraMacroOptions, RepeatReplaysUntilAStopAndReleasesHeldKeysAtEachBoundary) {
    macros({{M_DOWN(KC_X), M_DELAY('3', '0')}});
    set_flags(0, kRepeat | kRelease);
    press(m0);
    run(100);
    EXPECT_GE(presses(KC_X), 3U); // Each boundary released the DOWN before replaying it.
    release(m0);
    EXPECT_FALSE(held(KC_X));
    const unsigned stopped = presses(KC_X);
    run(200);
    EXPECT_EQ(presses(KC_X), stopped);
}

TEST_F(EraMacroOptions, RepeatWithoutAStopConditionPlaysOnce) {
    macros({{M_TAP(KC_X), M_DELAY('2', '0')}});
    set_flags(0, kRepeat);
    tap(m0);
    run(500);
    EXPECT_EQ(presses(KC_X), 1U);
    set_flags(0, kRepeat | kIgnoreMods);
    tap(m0);
    run(500);
    EXPECT_EQ(presses(KC_X), 2U);
}

TEST_F(EraMacroOptions, StopAndRepeatReleaseOnlyTheMacrosContributions) {
    macros({{M_DOWN(KC_A), M_DOWN(KC_LSFT), M_DOWN(KC_X), M_DELAY('5', '0')}});
    set_flags(0, kRepeat | kRelease);
    press(a);
    press(shift);
    press(m0);
    run(120); // Two repeat boundaries released the macro's A, Shift and X.
    EXPECT_GE(presses(KC_X), 3U);
    release(m0);
    EXPECT_FALSE(held(KC_X));
    EXPECT_TRUE(held(KC_A)); // The physical keys keep their own contributions.
    EXPECT_TRUE(get_mods() & MOD_BIT(KC_LSFT));
    release(a);
    release(shift);
    EXPECT_FALSE(held(KC_A));
    EXPECT_EQ(get_mods(), 0);
}

TEST_F(EraMacroOptions, IgnoreModifiersProjectsTheReportWithoutChangingModifierState) {
    macros({{M_DOWN(KC_X), M_DELAY('5', '0'), M_UP(KC_X)},
            {M_DOWN(KC_LCTL), M_DOWN(KC_X), M_DELAY('5', '0'), M_UP(KC_X), M_UP(KC_LCTL)},
            {M_DOWN(KC_X)}});
    press(shift);
    tap(m0); // Option off: the held Shift reaches the macro's output.
    ASSERT_TRUE(held(KC_X));
    EXPECT_EQ(report_mods(), MOD_BIT(KC_LSFT));
    run(100);

    set_flags(0, kIgnoreMods);
    tap(m0);
    ASSERT_TRUE(held(KC_X));
    EXPECT_EQ(report_mods(), 0);
    EXPECT_TRUE(get_mods() & MOD_BIT(KC_LSFT)); // A report projection, not a state change.
    run(100);
    EXPECT_FALSE(held(KC_X));
    EXPECT_EQ(report_mods(), MOD_BIT(KC_LSFT));

    set_flags(1, kIgnoreMods);
    tap(m1);
    ASSERT_TRUE(held(KC_X));
    EXPECT_EQ(report_mods(), MOD_BIT(KC_LCTL)); // The macro's own modifiers stay.
    run(100);
    EXPECT_EQ(report_mods(), MOD_BIT(KC_LSFT));

    set_flags(2, kIgnoreMods);
    tap(m2);
    run(50); // Completed, keeping its explicit DOWN.
    ASSERT_TRUE(held(KC_X));
    EXPECT_EQ(report_mods(), 0);
    era_macro_cancel(); // A system event releases the DOWN together with its projection.
    EXPECT_FALSE(held(KC_X));
    EXPECT_EQ(report_mods(), MOD_BIT(KC_LSFT));
    release(shift);
}

TEST_F(EraMacroOptions, IgnoreModifiersFollowsOnlyMacroOutputTheReportCarries) {
    macros({{M_DOWN(KC_A)}}); // Completes and keeps its explicit DOWN.
    set_flags(0, kIgnoreMods);
    ASSERT_TRUE(era_socd_set_enabled(ERA_SOCD_PAIR_LR, true));
    ASSERT_TRUE(era_socd_set_keycode(ERA_SOCD_PAIR_LR, 0, KC_A));
    ASSERT_TRUE(era_socd_set_keycode(ERA_SOCD_PAIR_LR, 1, KC_D)); // The last input wins.
    press(shift);
    tap(m0);
    run(5);
    ASSERT_TRUE(has(reports.back(), KC_A));
    EXPECT_EQ(reports.back().mods, 0);
    press(d); // SOCD takes the macro's only output out of the report ...
    EXPECT_FALSE(has(reports.back(), KC_A));
    EXPECT_TRUE(has(reports.back(), KC_D));
    EXPECT_EQ(reports.back().mods, MOD_BIT(KC_LSFT)); // ... so the held Shift reaches the user's own key.
    release(d);
    EXPECT_TRUE(has(reports.back(), KC_A));
    EXPECT_EQ(reports.back().mods, 0);
    era_macro_cancel();
    EXPECT_EQ(reports.back().mods, MOD_BIT(KC_LSFT));
    release(shift);
    era_socd_set_enabled(ERA_SOCD_PAIR_LR, false);
}

TEST_F(EraMacroOptions, PlaybackUsesTheOptionsTakenAtAdmission) {
    macros({{M_DOWN(KC_X), M_DELAY('1', '0', '0'), M_UP(KC_X)}});
    press(m0);
    set_flags(0, kRelease);
    release(m0);
    EXPECT_TRUE(held(KC_X));
    run(150);
    press(m0);
    release(m0);
    EXPECT_FALSE(held(KC_X));
}

TEST_F(EraMacroOptions, SetChangesRamOnlyAndSaveWritesOnlyChangedRecords) {
    EXPECT_EQ(set_flags(2, kRetap | kHold)[0], id_custom_set_value);
    EXPECT_EQ(set_hold(2, 0xFFFF)[0], id_custom_set_value);
    EXPECT_EQ(flags_of(2), kRetap | kHold);
    EXPECT_EQ(hold_of(2), 0xFFFF);
    EXPECT_EQ(table_writes, 0U);
    era_macro_options_init(); // Reload without SAVE.
    EXPECT_EQ(flags_of(2), 0);
    EXPECT_EQ(hold_of(2), 1000);

    set_flags(2, kRetap | kHold);
    set_hold(2, 0xFFFF);
    EXPECT_EQ(save()[0], id_custom_save);
    era_macro_options_init();
    EXPECT_EQ(flags_of(2), kRetap | kHold);
    EXPECT_EQ(hold_of(2), 0xFFFF); // A valid 65535 ms.
    EXPECT_EQ(flags_of(3), 0);
    EXPECT_EQ(hold_of(3), 1000);

    table_writes = 0;
    EXPECT_EQ(save()[0], id_custom_save); // Nothing unsaved.
    set_flags(2, kRetap | kHold);         // The same value.
    EXPECT_EQ(save()[0], id_custom_save);
    set_flags(2, kOther);
    set_flags(2, kRetap | kHold); // Changed back before SAVE.
    EXPECT_EQ(save()[0], id_custom_save);
    EXPECT_EQ(table_writes, 0U);
    set_flags(5, kRepeat | kRelease);
    EXPECT_EQ(save()[0], id_custom_save);
    EXPECT_EQ(table_writes, 1U); // That record alone.
}

TEST_F(EraMacroOptions, FailedSaveAnswersUnhandledAndKeepsTheUnsavedIntent) {
    set_hold(5, 42);
    fail_writes = true;
    EXPECT_EQ(save()[0], id_unhandled);
    EXPECT_EQ(hold_of(5), 42);
    fail_writes = false;
    EXPECT_EQ(save()[0], id_custom_save);
    era_macro_options_init();
    EXPECT_EQ(hold_of(5), 42);
}

TEST_F(EraMacroOptions, ConfigAdvancesOnlyWhenAGetWouldReturnAChangedValue) {
    const uint32_t base = era_state_sync_config_revision();
    set_flags(1, kOther);
    EXPECT_EQ(era_state_sync_config_revision(), base + 1);
    set_flags(1, kOther);
    set_hold(1, 1000); // Both unchanged.
    EXPECT_EQ(era_state_sync_config_revision(), base + 1);
    set_hold(1, 250);
    EXPECT_EQ(era_state_sync_config_revision(), base + 2);
    EXPECT_EQ(set_hold(1, 0)[0], id_unhandled);
    EXPECT_EQ(era_state_sync_config_revision(), base + 2);
    EXPECT_EQ(save()[0], id_custom_save); // SAVE persists what SET already published.
    EXPECT_GT(table_writes, 0U);
    EXPECT_EQ(era_state_sync_config_revision(), base + 2);
}

TEST_F(EraMacroOptions, MacroResetAndContentEditsKeepOptionsWhileAnEepromClearRestoresDefaults) {
    macros({{'a'}});
    set_flags(0, kRelease);
    set_hold(0, 300);
    ASSERT_EQ(save()[0], id_custom_save);

    EXPECT_EQ(via({id_dynamic_keymap_macro_reset})[0], id_dynamic_keymap_macro_reset);
    const uint16_t marker = dynamic_keymap_macro_get_buffer_size() - 1U;
    for (const Frame &upload : {via({id_dynamic_keymap_macro_set_buffer, uint8_t(marker >> 8), uint8_t(marker), 1, 0xFF}),
                                via({id_dynamic_keymap_macro_set_buffer, 0, 0, 2, 'b', 0}),
                                via({id_dynamic_keymap_macro_set_buffer, uint8_t(marker >> 8), uint8_t(marker), 1, 0})}) {
        EXPECT_EQ(upload[0], id_dynamic_keymap_macro_set_buffer);
    }
    EXPECT_EQ(flags_of(0), kRelease);
    EXPECT_EQ(hold_of(0), 300);
    era_macro_options_init();
    EXPECT_EQ(flags_of(0), kRelease);
    EXPECT_EQ(hold_of(0), 300);

    eeconfig_init(); // An EEPROM clear that clears the macros.
    EXPECT_EQ(flags_of(0), 0);
    EXPECT_EQ(hold_of(0), 1000);
    era_macro_options_init();
    EXPECT_EQ(flags_of(0), 0);
    EXPECT_EQ(hold_of(0), 1000);
}

TEST_F(EraMacroOptions, RefusalsAnswerUnhandledAndChangeNothing) {
    const Frame   caps       = via({id_custom_get_value, kChannel, ERA_WIRE_MACRO_OPTIONS_CAPABILITIES});
    const uint8_t expected[] = {0x08, 0x11, 0x01, 0x01, 0x3F, 0x45, 0x4D, 0x4F, 0x31};
    EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), caps.begin()));

    set_flags(3, kOther);
    set_hold(3, 700);
    const uint32_t config  = era_state_sync_config_revision();
    auto           refused = [this](std::initializer_list<uint8_t> frame, uint8_t length = 32) { EXPECT_EQ(via(frame, length)[0], id_unhandled); };
    refused({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_CAPABILITIES, 0x01, 0x3F}); // GET only.
    refused({id_custom_set_value, kChannel, 0x04, 3, kRetap});
    refused({id_custom_get_value, kChannel, 0x04, 3});
    refused({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_FLAGS, kCount, kRetap});
    refused({id_custom_get_value, kChannel, ERA_WIRE_MACRO_OPTIONS_FLAGS, kCount});
    refused({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_HOLD_MS, kCount, 0x01, 0x00});
    refused({id_custom_get_value, kChannel, ERA_WIRE_MACRO_OPTIONS_HOLD_MS, 0xFF});
    refused({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_FLAGS, 3, 0x40});
    refused({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_FLAGS, 3, kRetap | 0x80});
    refused({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_HOLD_MS, 3, 0x00, 0x00}); // Never clamped.
    refused({id_custom_set_value, kChannel, ERA_WIRE_MACRO_OPTIONS_FLAGS, 3, kRetap}, 5);    // Short frames.
    refused({id_custom_get_value, kChannel, ERA_WIRE_MACRO_OPTIONS_CAPABILITIES}, 3);
    refused({id_custom_save, kChannel}, 2);
    EXPECT_EQ(flags_of(3), kOther);
    EXPECT_EQ(hold_of(3), 700);
    EXPECT_EQ(era_state_sync_config_revision(), config);
}

TEST_F(EraMacroOptions, InvalidStorageRestoresDefaults) {
    auto load = [](const std::vector<uint8_t> &bytes) {
        eeprom_update_block(bytes.data(), (void *)kTable, bytes.size());
        era_macro_options_init();
    };
    std::vector<uint8_t> table(kTableBytes, 0xFF); // Erased.
    load(table);
    for (uint8_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(flags_of(i), 0);
        EXPECT_EQ(hold_of(i), 1000);
    }

    const uint32_t signature = ERA_STORAGE_MACRO_OPTIONS_SIGNATURE;
    const uint8_t  header[]  = {uint8_t(signature), uint8_t(signature >> 8), uint8_t(signature >> 16), uint8_t(signature >> 24), ERA_STORAGE_MACRO_OPTIONS_VERSION, kCount, 0, 0};
    std::copy(std::begin(header), std::end(header), table.begin());
    auto record = [&table](uint8_t index, uint8_t flags, uint16_t hold) {
        uint8_t *at = &table[ERA_STORAGE_MACRO_OPTIONS_HEADER_BYTES + index * ERA_STORAGE_MACRO_OPTIONS_RECORD_BYTES];
        at[0]       = flags;
        at[1]       = uint8_t(hold);
        at[2]       = uint8_t(hold >> 8);
    };
    record(0, kRetap, 300);
    record(1, kOther | 0x40, 300); // A reserved bit defaults that record alone.
    record(2, kOther, 0);          // So does a zero hold.
    record(3, kIgnoreMods, 0xFFFF);
    load(table);
    EXPECT_EQ(flags_of(0), kRetap);
    EXPECT_EQ(hold_of(0), 300);
    EXPECT_EQ(flags_of(1), 0);
    EXPECT_EQ(hold_of(1), 1000);
    EXPECT_EQ(flags_of(2), 0);
    EXPECT_EQ(hold_of(2), 1000);
    EXPECT_EQ(flags_of(3), kIgnoreMods);
    EXPECT_EQ(hold_of(3), 0xFFFF);
    EXPECT_EQ(flags_of(4), 0); // Erased record bytes carry reserved bits.
    EXPECT_EQ(hold_of(4), 1000);

    const struct {
        size_t  offset;
        uint8_t value;
    } broken[] = {{0, uint8_t(signature + 1)}, {4, ERA_STORAGE_MACRO_OPTIONS_VERSION + 1}, {5, kCount - 1}, {6, 1}, {7, 1}};
    for (const auto &change : broken) {
        std::vector<uint8_t> bad = table;
        bad[change.offset]       = change.value;
        load(bad);
        EXPECT_EQ(flags_of(0), 0) << change.offset;
        EXPECT_EQ(hold_of(0), 1000) << change.offset;
    }

    set_flags(6, kRelease); // SAVE over a bad header rewrites every record.
    ASSERT_EQ(save()[0], id_custom_save);
    era_macro_options_init();
    EXPECT_EQ(flags_of(6), kRelease);
    EXPECT_EQ(flags_of(0), 0);
    EXPECT_EQ(hold_of(0), 1000);
}
