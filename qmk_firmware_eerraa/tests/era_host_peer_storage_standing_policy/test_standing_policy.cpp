// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/split/era_host_peer_storage_standing_policy.h"
}

TEST(EraHostPeerStorageStandingPolicy, NormalServicedRuntimeKeepsCadence) {
    EXPECT_FALSE(era_host_peer_storage_standing_policy_suppressed(false, true, false, false));
}

TEST(EraHostPeerStorageStandingPolicy, TransferExclusivitySuppressesCadence) {
    EXPECT_TRUE(era_host_peer_storage_standing_policy_suppressed(true, true, false, false));
}

TEST(EraHostPeerStorageStandingPolicy, PushApplyWaitSuppressesInitiatorCadence) {
    EXPECT_TRUE(era_host_peer_storage_standing_policy_suppressed(false, true, true, false));
}

TEST(EraHostPeerStorageStandingPolicy, PushCompleteWaitSuppressesInitiatorCadence) {
    EXPECT_TRUE(era_host_peer_storage_standing_policy_suppressed(false, true, false, true));
}

TEST(EraHostPeerStorageStandingPolicy, ResponderPushStateDoesNotCreateInitiatorSuppression) {
    EXPECT_FALSE(era_host_peer_storage_standing_policy_suppressed(false, false, true, false));
    EXPECT_FALSE(era_host_peer_storage_standing_policy_suppressed(false, false, false, true));
}

TEST(EraHostPeerStorageStandingPolicy, OverlappingReasonsRemainSuppressed) {
    EXPECT_TRUE(era_host_peer_storage_standing_policy_suppressed(true, true, true, false));
}
