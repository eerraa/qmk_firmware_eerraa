// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/split/era_host_peer_storage_indicator_policy.h"
}

TEST(EraHostPeerStorageIndicatorPolicy, ServicedRelationIsContinuous) {
    EXPECT_TRUE(era_host_peer_storage_indicator_relation_continuous(true, false));
}

TEST(EraHostPeerStorageIndicatorPolicy, FastRecoveryPreservesContinuityAcrossTemporaryNoLink) {
    EXPECT_TRUE(era_host_peer_storage_indicator_relation_continuous(false, true));
}

TEST(EraHostPeerStorageIndicatorPolicy, BackedOffNoLinkRetiresContinuity) {
    EXPECT_FALSE(era_host_peer_storage_indicator_relation_continuous(false, false));
}

TEST(EraHostPeerStorageIndicatorPolicy, ConfirmedOneHoldsLocalFallUntilConfirmedZero) {
    uint8_t bits = ERA_HOST_PEER_STORAGE_INDICATOR_GATE;

    bits = era_host_peer_storage_indicator_note_local_sent(bits, true);
    EXPECT_NE(bits & ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING, 0);
    EXPECT_TRUE(era_host_peer_storage_indicator_pair_pending(bits, false));

    bits = era_host_peer_storage_indicator_note_local_sent(bits, false);
    EXPECT_EQ(bits & ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING, 0);
    EXPECT_FALSE(era_host_peer_storage_indicator_pair_pending(bits, false));
}

TEST(EraHostPeerStorageIndicatorPolicy, UnsentOneCreatesNoSyntheticHold) {
    uint8_t bits = ERA_HOST_PEER_STORAGE_INDICATOR_GATE;
    EXPECT_TRUE(era_host_peer_storage_indicator_pair_pending(bits, true));
    EXPECT_FALSE(era_host_peer_storage_indicator_pair_pending(bits, false));
    EXPECT_EQ(bits & ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING, 0);
}

TEST(EraHostPeerStorageIndicatorPolicy, ClosedGateCannotLatchConfirmedOne) {
    uint8_t bits = 0;
    bits = era_host_peer_storage_indicator_note_local_sent(bits, true);
    EXPECT_EQ(bits & ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING, 0);
    EXPECT_FALSE(era_host_peer_storage_indicator_pair_pending(bits, false));
}

TEST(EraHostPeerStorageIndicatorPolicy, PolicyClosePreservesExistingConfirmedOneUntilZero) {
    uint8_t bits = ERA_HOST_PEER_STORAGE_INDICATOR_GATE;
    bits = era_host_peer_storage_indicator_note_local_sent(bits, true);
    ASSERT_NE(bits & ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING, 0);

    bits &= (uint8_t)~ERA_HOST_PEER_STORAGE_INDICATOR_GATE;
    bits = era_host_peer_storage_indicator_note_local_sent(bits, true);
    EXPECT_NE(bits & ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING, 0);
    EXPECT_TRUE(era_host_peer_storage_indicator_pair_pending(bits, false));

    bits = era_host_peer_storage_indicator_note_local_sent(bits, false);
    EXPECT_EQ(bits & ERA_HOST_PEER_STORAGE_INDICATOR_LOCAL_SENT_PENDING, 0);
    EXPECT_FALSE(era_host_peer_storage_indicator_pair_pending(bits, false));
}

TEST(EraHostPeerStorageIndicatorPolicy, PeerMirrorStillHoldsPairPendingIndependently) {
    uint8_t bits = ERA_HOST_PEER_STORAGE_INDICATOR_PEER_PENDING;
    EXPECT_TRUE(era_host_peer_storage_indicator_pair_pending(bits, false));
}
