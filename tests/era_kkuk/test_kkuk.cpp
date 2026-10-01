// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>

#include "keyboard_report_util.hpp"
#include "test_common.hpp"

extern "C" {
#include "keyboards/era/common/features/era_kkuk.h"
#define _Static_assert static_assert
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#undef _Static_assert
}

using testing::_;
using testing::InSequence;

namespace {

std::array<uint8_t, ERA_EEPROM_CONFIG_SIZE> g_era_config{};

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

class Kkuk : public TestFixture {
   protected:
    void SetUp() override {
        g_era_config.fill(0);
        era_kkuk_init();
        era_kkuk_set_enabled(true);
    }
};

// The default 200 ms delay arms KKUK; its first pulse then waits one repeat
// interval (80 ms), as on H7S, instead of firing the moment it arms.
TEST_F(Kkuk, FirstRepeatWaitsOneRepeatInterval) {
    TestDriver driver;
    InSequence s;
    auto       key_a = KeymapKey(0, 0, 0, KC_A);
    auto       key_b = KeymapKey(0, 1, 0, KC_B);
    set_keymap({key_a, key_b});
    ASSERT_EQ(era_kkuk_get_delay_ticks(), 20);
    ASSERT_EQ(era_kkuk_get_repeat_ticks(), 8);

    EXPECT_REPORT(driver, (KC_A));
    key_a.press();
    run_one_scan_loop();
    EXPECT_REPORT(driver, (KC_A, KC_B));
    key_b.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    EXPECT_NO_REPORT(driver);
    idle_for(200 + 70);
    VERIFY_AND_CLEAR(driver);

    EXPECT_EMPTY_REPORT(driver);
    EXPECT_REPORT(driver, (KC_A, KC_B));
    idle_for(20);
    VERIFY_AND_CLEAR(driver);

    EXPECT_REPORT(driver, (KC_A));
    key_b.release();
    run_one_scan_loop();
    EXPECT_EMPTY_REPORT(driver);
    key_a.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
}
