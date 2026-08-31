// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/split/era_split_rgb_sleep_policy.h"
}

TEST(EraSplitRgbSleepPolicy, ThreeIndependentLocalReasonsAreOrCombined) {
    EXPECT_FALSE(era_split_rgb_sleep_policy_local_requested(false, false, 60, 59999));
    EXPECT_TRUE(era_split_rgb_sleep_policy_local_requested(true, false, 60, 0));
    EXPECT_TRUE(era_split_rgb_sleep_policy_local_requested(false, true, 60, 0));
    EXPECT_TRUE(era_split_rgb_sleep_policy_local_requested(false, false, 60, 60000));
}

TEST(EraSplitRgbSleepPolicy, UnsupportedZeroTimeoutDoesNotCreateIdleSleep) {
    EXPECT_FALSE(era_split_rgb_sleep_policy_local_requested(false, false, 0, UINT32_MAX));
}

TEST(EraSplitRgbSleepPolicy, StockPresetAcceptsOnlyTheSixProductChoices) {
    const uint8_t valid[] = {1, 3, 5, 10, 30, 60};
    for (uint8_t value : valid) {
        EXPECT_TRUE(era_split_rgb_sleep_policy_preset_valid(value));
    }
    EXPECT_FALSE(era_split_rgb_sleep_policy_preset_valid(0));
    EXPECT_FALSE(era_split_rgb_sleep_policy_preset_valid(2));
    EXPECT_FALSE(era_split_rgb_sleep_policy_preset_valid(61));
}

TEST(EraSplitRgbSleepPolicy, ExactSecondsProjectDownWithoutMutation) {
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(1), 1);
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(179), 1);
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(180), 3);
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(599), 5);
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(600), 10);
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(3599), 30);
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(3600), 60);
    EXPECT_EQ(era_split_rgb_sleep_policy_preset_minutes(UINT16_MAX), 60);
}
