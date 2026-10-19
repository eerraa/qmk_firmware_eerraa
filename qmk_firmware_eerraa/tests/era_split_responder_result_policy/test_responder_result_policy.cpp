// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

#define _Static_assert static_assert
extern "C" {
#include "keyboards/era/common/split/communication_core/era_split_communication_core_responder_result_policy.h"
}
#undef _Static_assert

static era_split_communication_core_responder_snapshot_t snapshot() {
    era_split_communication_core_responder_snapshot_t value{};
    value.owner_epoch          = 7;
    value.relation_generation  = 11;
    value.snapshot_generation  = 13;
    value.response_section_mask = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS;
    return value;
}

static era_split_communication_core_responder_result_t matching_result() {
    era_split_communication_core_responder_result_t value{};
    value.owner_epoch          = 7;
    value.relation_generation  = 11;
    value.snapshot_generation  = 13;
    value.kind                 = ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_HEARTBEAT;
    value.response_sent        = 1;
    value.result               = ERA_SPLIT_TRANSACTION_RESULT_OK;
    value.response_section_mask = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS;
    return value;
}

TEST(EraSplitResponderResultPolicy, ExactSuccessfulHeartbeatCoversSnapshot) {
    auto snap = snapshot();
    auto result = matching_result();
    EXPECT_TRUE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
}

TEST(EraSplitResponderResultPolicy, EmptySectionMaskNeverCoalesces) {
    auto snap = snapshot();
    auto result = matching_result();
    snap.response_section_mask = 0;
    result.response_section_mask = 0;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
}

TEST(EraSplitResponderResultPolicy, DifferentSnapshotIdentityNeverCoalesces) {
    auto snap = snapshot();
    auto result = matching_result();
    result.snapshot_generation++;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
}

TEST(EraSplitResponderResultPolicy, DifferentWireSectionMaskNeverCoalesces) {
    auto snap = snapshot();
    auto result = matching_result();
    result.response_section_mask = ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_AUTHORITY;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
}

TEST(EraSplitResponderResultPolicy, FailedOrUnsentHeartbeatNeverCoalesces) {
    auto snap = snapshot();
    auto result = matching_result();
    result.response_sent = 0;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
    result.response_sent = 1;
    result.result = ERA_SPLIT_TRANSACTION_RESULT_FAIL;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
}

TEST(EraSplitResponderResultPolicy, SessionAndPushResultsNeverCoalesce) {
    auto snap = snapshot();
    auto result = matching_result();
    result.kind = ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SESSION;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
    result.kind = ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_RUNTIME_PUSH;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
    result.kind = ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_RESULT_SOURCE_PUSH;
    EXPECT_FALSE(era_split_communication_core_responder_heartbeat_result_covers_snapshot(&snap, &result));
}
