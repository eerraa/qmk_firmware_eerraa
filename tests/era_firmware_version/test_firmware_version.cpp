// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

#include <array>
#include <cstring>

extern "C" {
#include "keyboards/era/common/system/era_common_via.h"
#include "keyboards/era/common/system/era_firmware_version.h"
#include "keyboards/era/common/system/era_via_system.h"
#include "keyboards/era/common/features/era_rgb_sleep.h"
#include "eeconfig.h"
#include "keycode_config.h"
#include "via.h"
}

static uint32_t g_system_handler_calls;

extern "C" bool era_via_system_handle_via_command(uint8_t *data, uint8_t) {
    g_system_handler_calls++;
    return data && data[1] == ERA_VIA_SYSTEM_CHANNEL;
}

extern "C" bool process_record_via(uint16_t, keyrecord_t *) {
    return true;
}

extern "C" bool via_eeprom_is_valid(void) {
    return true;
}

extern "C" void via_init(void) {}
extern "C" void eeconfig_init_via(void) {}

namespace {

constexpr size_t  kReportSize = 32U;
constexpr uint8_t kSentinel   = 0xA5U;

using Report = std::array<uint8_t, kReportSize>;

Report command_report(uint8_t command, uint8_t channel, uint8_t value_id) {
    Report report;
    report.fill(kSentinel);
    report[0] = command;
    report[1] = channel;
    report[2] = value_id;
    return report;
}

void expect_handler_declines_unchanged(uint8_t command, uint8_t channel, uint8_t value_id) {
    Report report = command_report(command, channel, value_id);
    Report before = report;

    EXPECT_FALSE(era_firmware_version_handle_via_command(report.data(), report.size()));
    EXPECT_EQ(report, before);
}

} // namespace

TEST(EraFirmwareVersion, CanonicalIdentityAndCompleteNulTerminatedPayload) {
    static constexpr char kExpected[] = "260901R1";

    EXPECT_STREQ(ERA_FIRMWARE_VERSION, kExpected);
    EXPECT_STREQ(era_firmware_version, kExpected);
    EXPECT_EQ(std::strlen(era_firmware_version), ERA_FIRMWARE_VERSION_LENGTH);
    EXPECT_EQ(sizeof(ERA_FIRMWARE_VERSION), sizeof(kExpected));
    EXPECT_EQ(sizeof(kExpected), ERA_FIRMWARE_VERSION_PAYLOAD_SIZE);

    Report report = command_report(id_custom_get_value, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID);
    ASSERT_TRUE(era_firmware_version_handle_via_command(report.data(), report.size()));

    EXPECT_EQ(report[0], id_custom_get_value);
    EXPECT_EQ(report[1], ERA_VIA_FIRMWARE_VERSION_CHANNEL);
    EXPECT_EQ(report[2], ERA_VIA_FIRMWARE_VERSION_VALUE_ID);
    EXPECT_EQ(std::memcmp(&report[3], kExpected, sizeof(kExpected)), 0);
    EXPECT_EQ(report[3 + sizeof(kExpected)], kSentinel);
}

TEST(EraFirmwareVersion, CommonSystemRouterClaimsVersionBeforeOtherSystemChannels) {
    Report report = command_report(id_custom_get_value, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID);
    g_system_handler_calls = 0U;

    ASSERT_TRUE(era_common_via_handle_system_command(report.data(), report.size()));
    EXPECT_STREQ(reinterpret_cast<const char *>(&report[3]), ERA_FIRMWARE_VERSION);
    EXPECT_EQ(g_system_handler_calls, 0U);
}

TEST(EraFirmwareVersion, ChannelNineFallsThroughToItsExistingOwner) {
    Report report = command_report(id_custom_get_value, ERA_VIA_SYSTEM_CHANNEL, ERA_VIA_SYSTEM_BOOTLOADER_VALUE_ID);
    Report before = report;
    g_system_handler_calls = 0U;

    EXPECT_FALSE(era_firmware_version_handle_via_command(report.data(), report.size()));
    EXPECT_EQ(report, before);
    EXPECT_TRUE(era_common_via_handle_system_command(report.data(), report.size()));
    EXPECT_EQ(report, before);
    EXPECT_EQ(g_system_handler_calls, 1U);
}

TEST(EraFirmwareVersion, RgbSleepMasterDefaultsOnAndPersistsWithoutDisturbingKeymapFlags) {
    keymap_config.raw             = 0;
    keymap_config.swap_lctl_lgui = true;
    eeconfig_update_keymap(&keymap_config);

    Report get = command_report(id_custom_get_value, ERA_VIA_SYSTEM_CHANNEL, ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID);
    g_system_handler_calls = 0U;
    ASSERT_TRUE(era_common_via_handle_system_command(get.data(), get.size()));
    EXPECT_EQ(get[3], 1U);
    EXPECT_EQ(g_system_handler_calls, 0U);

    Report disable = command_report(id_custom_set_value, ERA_VIA_SYSTEM_CHANNEL, ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID);
    disable[3] = 0U;
    ASSERT_TRUE(era_common_via_handle_system_command(disable.data(), disable.size()));
    EXPECT_EQ(disable[3], 0U);
    EXPECT_FALSE(era_rgb_sleep_enabled());
    EXPECT_TRUE(keymap_config.swap_lctl_lgui);

    keymap_config_t stored = {};
    eeconfig_read_keymap(&stored);
    EXPECT_TRUE(stored.era_rgb_sleep_disabled);
    EXPECT_TRUE(stored.swap_lctl_lgui);

    Report enable = command_report(id_custom_set_value, ERA_VIA_SYSTEM_CHANNEL, ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID);
    enable[3] = 1U;
    ASSERT_TRUE(era_common_via_handle_system_command(enable.data(), enable.size()));
    EXPECT_EQ(enable[3], 1U);
    EXPECT_TRUE(era_rgb_sleep_enabled());

    eeconfig_read_keymap(&stored);
    EXPECT_FALSE(stored.era_rgb_sleep_disabled);
    EXPECT_TRUE(stored.swap_lctl_lgui);
    EXPECT_EQ(sizeof(keymap_config_t), 2U);
}

TEST(EraFirmwareVersion, RgbSleepMasterRejectsWrongAddressShortPacketAndSave) {
    Report wrong = command_report(id_custom_get_value, ERA_VIA_SYSTEM_CHANNEL, ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID - 1U);
    Report before = wrong;
    EXPECT_FALSE(era_rgb_sleep_handle_via_command(wrong.data(), wrong.size()));
    EXPECT_EQ(wrong, before);

    Report short_report = command_report(id_custom_get_value, ERA_VIA_SYSTEM_CHANNEL, ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID);
    before = short_report;
    EXPECT_FALSE(era_rgb_sleep_handle_via_command(short_report.data(), 3U));
    EXPECT_EQ(short_report, before);

    Report save = command_report(id_custom_save, ERA_VIA_SYSTEM_CHANNEL, ERA_VIA_SYSTEM_RGB_SLEEP_ENABLE_VALUE_ID);
    before = save;
    EXPECT_FALSE(era_rgb_sleep_handle_via_command(save.data(), save.size()));
    EXPECT_EQ(save, before);
}

TEST(EraFirmwareVersion, SetSaveAndInvalidAddressesRemainUnhandledAndUnchanged) {
    const struct {
        uint8_t command;
        uint8_t channel;
        uint8_t value_id;
    } cases[] = {
        {id_custom_set_value, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID},
        {id_custom_save, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID},
        {id_custom_get_value, ERA_VIA_FIRMWARE_VERSION_CHANNEL - 1U, ERA_VIA_FIRMWARE_VERSION_VALUE_ID},
        {id_custom_get_value, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID - 1U},
        {id_custom_get_value, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID + 1U},
        {id_get_protocol_version, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID},
        {0xFEU, ERA_VIA_FIRMWARE_VERSION_CHANNEL, ERA_VIA_FIRMWARE_VERSION_VALUE_ID},
    };

    for (const auto &test_case : cases) {
        expect_handler_declines_unchanged(test_case.command, test_case.channel, test_case.value_id);

        Report report = command_report(test_case.command, test_case.channel, test_case.value_id);
        Report before = report;
        EXPECT_FALSE(era_common_via_handle_system_command(report.data(), report.size()));
        EXPECT_EQ(report, before);
    }
}

TEST(EraFirmwareVersion, NullReportIsUnhandled) {
    EXPECT_FALSE(era_firmware_version_handle_via_command(nullptr, 0U));
}
