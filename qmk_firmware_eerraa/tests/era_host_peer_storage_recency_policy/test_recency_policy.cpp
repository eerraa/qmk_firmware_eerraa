// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/split/era_host_peer_storage_recency_policy.h"
}

TEST(EraHostPeerStorageRecencyPolicy, IncrementFailureCannotPublishOrSignalDeparture) {
    auto result = era_host_peer_storage_recency_settle_result(false, true, false);

    EXPECT_EQ(result, ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_PERSIST_FAILED);
    EXPECT_FALSE(era_host_peer_storage_recency_settle_can_publish(result));
    EXPECT_FALSE(era_host_peer_storage_recency_settle_departed(result));
}

TEST(EraHostPeerStorageRecencyPolicy, ClearFailureCannotPublishAtBaseline) {
    auto result = era_host_peer_storage_recency_settle_result(true, true, false);

    EXPECT_EQ(result, ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_PERSIST_FAILED);
    EXPECT_FALSE(era_host_peer_storage_recency_settle_can_publish(result));
    EXPECT_FALSE(era_host_peer_storage_recency_settle_departed(result));
}

TEST(EraHostPeerStorageRecencyPolicy, SuccessfulSettlesExposeOnlyTheirDurableMeaning) {
    auto departed = era_host_peer_storage_recency_settle_result(false, true, true);
    auto baseline = era_host_peer_storage_recency_settle_result(true, true, true);

    EXPECT_TRUE(era_host_peer_storage_recency_settle_can_publish(departed));
    EXPECT_TRUE(era_host_peer_storage_recency_settle_departed(departed));
    EXPECT_TRUE(era_host_peer_storage_recency_settle_can_publish(baseline));
    EXPECT_FALSE(era_host_peer_storage_recency_settle_departed(baseline));
}

TEST(EraHostPeerStorageRecencyPolicy, SaturatedCounterNeedsNoWriteToPublishDeparture) {
    auto result = era_host_peer_storage_recency_settle_result(false, false, false);

    EXPECT_EQ(result, ERA_HOST_PEER_STORAGE_RECENCY_SETTLE_DEPARTED);
    EXPECT_TRUE(era_host_peer_storage_recency_settle_can_publish(result));
    EXPECT_TRUE(era_host_peer_storage_recency_settle_departed(result));
}

TEST(EraHostPeerStorageRecencyPolicy, FailedConvergencePublicationCannotRetireRecency) {
    EXPECT_FALSE(era_host_peer_storage_recency_convergence_can_retire(false));
    EXPECT_TRUE(era_host_peer_storage_recency_convergence_can_retire(true));
}
