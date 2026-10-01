// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>

#include "gtest/gtest.h"

extern "C" {
#define VIA_ENABLE
#include "keyboards/era/common/features/era_lighting_sleep.h"
#include "keyboards/era/common/system/era_via_system.h"
#include "via.h"
#define _Static_assert static_assert
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#undef _Static_assert
}

namespace {

std::array<uint8_t, ERA_EEPROM_CONFIG_SIZE> g_era_config{};
bool g_rgb_switch   = true;
int  g_rgb_gate     = -1;
int  g_backlight    = -1;
int  g_commit_count = 0;

std::array<uint8_t, 32> via(uint8_t command, uint8_t value_id, uint8_t b0 = 0, uint8_t b1 = 0) {
    std::array<uint8_t, 32> data{};
    data[0] = command;
    data[1] = ERA_VIA_SYSTEM_CHANNEL;
    data[2] = value_id;
    data[3] = b0;
    data[4] = b1;
    return data;
}

bool send(std::array<uint8_t, 32> &data) {
    return era_lighting_sleep_handle_via_command(data.data(), (uint8_t)data.size());
}

const uint8_t *record(void) {
    return g_era_config.data() + ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET;
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

void era_state_sync_note_config_semantic_commit(uint16_t offset, uint16_t length) {
    EXPECT_EQ(offset, ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET);
    EXPECT_EQ(length, ERA_EEPROM_LIGHTING_SLEEP_CONFIG_SIZE);
    g_commit_count++;
}

bool era_rgb_sleep_enabled(void) {
    return g_rgb_switch;
}

void era_lighting_sleep_test_rgb_gate(bool sleep) {
    g_rgb_gate = sleep ? 1 : 0;
}

void era_lighting_sleep_test_backlight_gate(bool sleep) {
    g_backlight = sleep ? 1 : 0;
}

} // extern "C"

class EraLightingSleep : public testing::Test {
   protected:
    void SetUp() override {
        g_era_config.fill(0);
        g_rgb_switch   = true;
        g_rgb_gate     = -1;
        g_backlight    = -1;
        g_commit_count = 0;
        era_lighting_sleep_init();
    }
};

TEST_F(EraLightingSleep, ZeroedRecordIsTheDefault) {
    EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 600U);
    EXPECT_EQ(era_lighting_sleep_backlight_timeout_seconds(), 600U);
    EXPECT_TRUE(era_lighting_sleep_backlight_enabled());
}

TEST_F(EraLightingSleep, UnknownFlagOrReservedByteResetsTheRecord) {
    for (size_t bad : {size_t(4), size_t(5), size_t(7)}) {
        g_era_config.fill(0);
        g_era_config[ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET + 0] = 60;  // rgb 60 s
        g_era_config[ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET + bad] = bad == 4 ? 0x02 : 0x01;
        era_lighting_sleep_reload_from_eeprom();
        EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 600U) << bad;
    }
    g_era_config.fill(0);
    g_era_config[ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET + 0] = 60;
    g_era_config[ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET + 4] = 0x01;
    era_lighting_sleep_reload_from_eeprom();
    EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 60U);
    EXPECT_FALSE(era_lighting_sleep_backlight_enabled());
}

TEST_F(EraLightingSleep, StockPresetsAndExactSecondsShareOneStore) {
    auto get = via(id_custom_get_value, ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID);
    ASSERT_TRUE(send(get));
    EXPECT_EQ(get[3], 10);

    auto set = via(id_custom_set_value, ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID, 3);
    ASSERT_TRUE(send(set));
    EXPECT_EQ(set[3], 3);
    EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 180U);
    EXPECT_EQ(record()[0] | (record()[1] << 8), 180) << "SET persists at once";
    EXPECT_EQ(g_commit_count, 1);

    auto bad = via(id_custom_set_value, ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID, 2);
    EXPECT_FALSE(send(bad)) << "a value no stock control offers is refused";
    EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 180U);

    auto exact = via(id_custom_set_value, ERA_VIA_SYSTEM_RGB_SLEEP_EXACT_SECONDS_VALUE_ID, 0, 125);
    ASSERT_TRUE(send(exact));
    EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 125U);
    auto projected = via(id_custom_get_value, ERA_VIA_SYSTEM_RGB_SLEEP_PRESET_VALUE_ID);
    ASSERT_TRUE(send(projected));
    EXPECT_EQ(projected[3], 1) << "the stock menu shows the preset below an exact value";
    EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 125U) << "GET never rewrites the exact value";

    auto zero = via(id_custom_set_value, ERA_VIA_SYSTEM_RGB_SLEEP_EXACT_SECONDS_VALUE_ID, 0, 0);
    EXPECT_FALSE(send(zero));
}

TEST_F(EraLightingSleep, BacklightHasItsOwnSwitchAndTimeout) {
    auto off = via(id_custom_set_value, ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_ENABLE_VALUE_ID, 0);
    ASSERT_TRUE(send(off));
    EXPECT_EQ(off[3], 0);
    EXPECT_FALSE(era_lighting_sleep_backlight_enabled());
    EXPECT_EQ(record()[4], 0x01);

    auto preset = via(id_custom_set_value, ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_PRESET_VALUE_ID, 30);
    ASSERT_TRUE(send(preset));
    EXPECT_EQ(era_lighting_sleep_backlight_timeout_seconds(), 1800U);
    EXPECT_EQ(era_lighting_sleep_rgb_timeout_seconds(), 600U) << "the RGB timeout is separate";

    auto exact = via(id_custom_get_value, ERA_VIA_SYSTEM_BACKLIGHT_SLEEP_EXACT_SECONDS_VALUE_ID);
    ASSERT_TRUE(send(exact));
    EXPECT_EQ((exact[3] << 8) | exact[4], 1800);

    auto rgb_switch = via(id_custom_get_value, ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID);
    EXPECT_FALSE(send(rgb_switch)) << "value 12 belongs to era_rgb_sleep.c";
}

TEST_F(EraLightingSleep, EachOutputSleepsOnItsOwnReasonsAndSwitch) {
    ASSERT_TRUE(era_lighting_sleep_set_rgb_timeout_seconds(60));
    ASSERT_TRUE(era_lighting_sleep_set_backlight_timeout_seconds(180));

    era_lighting_sleep_update(false, false, 59999);
    EXPECT_EQ(g_rgb_gate, 0);
    EXPECT_EQ(g_backlight, 0);
    era_lighting_sleep_update(false, false, 60000);
    EXPECT_EQ(g_rgb_gate, 1);
    EXPECT_EQ(g_backlight, 0) << "the backlight waits for its own timeout";
    era_lighting_sleep_update(false, false, 180000);
    EXPECT_EQ(g_backlight, 1);

    era_lighting_sleep_update(false, false, 0);
    EXPECT_EQ(g_rgb_gate, 0) << "input wakes both";
    EXPECT_EQ(g_backlight, 0);

    era_lighting_sleep_update(true, false, 0);
    EXPECT_EQ(g_rgb_gate, 1) << "USB suspend";
    EXPECT_EQ(g_backlight, 1);
    era_lighting_sleep_update(false, true, 0);
    EXPECT_EQ(g_rgb_gate, 1) << "host loss";
    EXPECT_EQ(g_backlight, 1);

    g_rgb_switch = false;
    ASSERT_TRUE(era_lighting_sleep_set_backlight_enabled(false));
    era_lighting_sleep_update(true, true, 10000000);
    EXPECT_EQ(g_rgb_gate, 0) << "a switch that is off disables every reason";
    EXPECT_EQ(g_backlight, 0);
}
