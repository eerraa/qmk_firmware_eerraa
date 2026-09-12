// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/split/era_split_rgb_sleep_policy.h"
#include "lighting_under_test.h"
}

TEST(EraSplitRgbSleepPolicy, ThreeIndependentLocalReasonsAreOrCombined) {
    EXPECT_FALSE(era_split_rgb_sleep_policy_local_requested(false, false, true, 60, 59999));
    EXPECT_TRUE(era_split_rgb_sleep_policy_local_requested(true, false, true, 60, 0));
    EXPECT_TRUE(era_split_rgb_sleep_policy_local_requested(false, true, true, 60, 0));
    EXPECT_TRUE(era_split_rgb_sleep_policy_local_requested(false, false, true, 60, 60000));
}

TEST(EraSplitRgbSleepPolicy, UnsupportedZeroTimeoutDoesNotCreateIdleSleep) {
    EXPECT_FALSE(era_split_rgb_sleep_policy_local_requested(false, false, true, 0, UINT32_MAX));
}

TEST(EraSplitRgbSleepPolicy, UserDisableGatesEveryRgbSleepReason) {
    EXPECT_FALSE(era_split_rgb_sleep_policy_local_requested(false, false, false, 60, UINT32_MAX));
    EXPECT_FALSE(era_split_rgb_sleep_policy_local_requested(true, false, false, 60, 0));
    EXPECT_FALSE(era_split_rgb_sleep_policy_local_requested(false, true, false, 60, 0));
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

TEST(EraSplitLightingOwner, CurrentWirePublicationSurvivesEitherSideOfFirstResolve) {
    for (bool resolve_first : {false, true}) {
        SCOPED_TRACE(resolve_first);
        era_test_lighting_reset();
        ASSERT_FALSE(era_test_lighting_resolve());
        // The scheduler committed PEER mode. A successful current-relation
        // RGB response may arrive before the resolver's next 1 kHz refresh.
        era_test_lighting_set_owner(true);
        if (resolve_first) {
            EXPECT_FALSE(era_test_lighting_resolve());
        }
        ASSERT_TRUE(era_test_lighting_publish(true));
        EXPECT_TRUE(era_test_lighting_resolve());
        EXPECT_TRUE(era_test_lighting_resolve());
    }
}

TEST(EraSplitLightingOwner, WireCannotOverrideLocalOwnerAndDemotionDropsLocalSleep) {
    era_test_lighting_reset();
    era_test_lighting_set_local_loss(true);
    EXPECT_TRUE(era_test_lighting_resolve());
    era_test_lighting_publish(false);
    EXPECT_TRUE(era_test_lighting_resolve());
    era_test_lighting_set_owner(true);
    EXPECT_FALSE(era_test_lighting_resolve());
    era_test_lighting_publish(true);
    EXPECT_TRUE(era_test_lighting_resolve());
    era_test_lighting_set_local_loss(false);
    era_test_lighting_set_owner(false);
    EXPECT_FALSE(era_test_lighting_resolve());
}

TEST(EraSplitLightingOwner, RotationRetiresPreviousWordEvenWhenPeerRoleIsUnchanged) {
    for (bool publish_before_refresh : {false, true}) {
        SCOPED_TRACE(publish_before_refresh);
        era_test_lighting_reset();
        era_test_lighting_set_owner(true);
        ASSERT_TRUE(era_test_lighting_publish(true));
        ASSERT_TRUE(era_test_lighting_resolve());
        era_test_lighting_rotate_relation();
        if (!publish_before_refresh) {
            EXPECT_FALSE(era_test_lighting_resolve());
        }
        ASSERT_TRUE(era_test_lighting_publish(true));
        EXPECT_TRUE(era_test_lighting_resolve());
    }
}

TEST(EraSplitLightingOwner, RoleRoundTripBetweenRefreshesCannotRetainOldWireWord) {
    era_test_lighting_reset();
    era_test_lighting_set_owner(true);
    ASSERT_TRUE(era_test_lighting_publish(true));
    ASSERT_TRUE(era_test_lighting_resolve());
    era_test_lighting_rotate_relation();
    era_test_lighting_set_owner(false);
    era_test_lighting_rotate_relation();
    era_test_lighting_set_owner(true);
    EXPECT_FALSE(era_test_lighting_resolve());
}

TEST(EraSplitLightingOwner, NonOwnerPublicationIsRejectedRatherThanCached) {
    era_test_lighting_reset();
    EXPECT_FALSE(era_test_lighting_publish(true));
    EXPECT_FALSE(era_test_lighting_resolve());
    era_test_lighting_set_owner(true);
    EXPECT_FALSE(era_test_lighting_resolve());
    ASSERT_TRUE(era_test_lighting_publish(true));
    era_test_lighting_set_owner(false);
    EXPECT_FALSE(era_test_lighting_publish(true));
    EXPECT_FALSE(era_test_lighting_resolve());
}
