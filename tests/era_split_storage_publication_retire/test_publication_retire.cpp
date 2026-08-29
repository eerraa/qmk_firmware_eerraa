// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

#define _Static_assert(...)
extern "C" {
#include "keyboards/era/common/split/communication_core/era_split_communication_core_storage.h"
#include "storage_under_test.h"
}
#undef _Static_assert

namespace {

constexpr uint16_t kGeneration = 7U;

era_split_communication_core_storage_initiator_request_t request_for(uint16_t generation = kGeneration) {
    era_split_communication_core_storage_initiator_request_t request{};
    request.owner_epoch            = 1U;
    request.relation_generation    = 2U;
    request.request_generation     = generation;
    request.policy_generation      = 3U;
    request.transaction_generation = 4U;
    request.image_size             = ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES;
    request.response_window_ms     = ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS;
    request.domain                 = ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG;
    request.schema                 = ERA_HOST_PEER_STORAGE_SCHEMA_V1;
    request.operation              = ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ;
    return request;
}

era_split_communication_core_storage_responder_snapshot_t snapshot_for(uint16_t generation = kGeneration) {
    era_split_communication_core_storage_responder_snapshot_t snapshot{};
    snapshot.owner_epoch            = 1U;
    snapshot.relation_generation    = 2U;
    snapshot.snapshot_generation    = generation;
    snapshot.policy_generation      = 3U;
    snapshot.transaction_generation = 4U;
    snapshot.image_size             = ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES;
    snapshot.domain                 = ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG;
    snapshot.schema                 = ERA_HOST_PEER_STORAGE_SCHEMA_V1;
    snapshot.expected_operation     = ERA_SPLIT_EEPROM_SYNC_OP_PROBE_REQ;
    return snapshot;
}

class EraSplitStoragePublicationRetire : public ::testing::Test {
   protected:
    void SetUp() override {
        era_test_storage_boot_reset();
        era_test_storage_set_time_us(100000U);
    }
};

} // namespace

TEST_F(EraSplitStoragePublicationRetire, IdleRetirementIsTerminalAcrossCapacityReset) {
    ASSERT_TRUE(era_split_communication_core_storage_retire_publications());
    EXPECT_TRUE(era_split_communication_core_storage_retire_publications());

    /* Runtime owner changes may reset capacity. Retirement is boot-terminal,
       so that reset must preserve both terminal source sequences. */
    era_split_communication_core_storage_capacity_init();

    auto request = request_for();
    EXPECT_FALSE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    EXPECT_FALSE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    EXPECT_FALSE(era_split_communication_core_storage_claim_initiator_request(&request));

    auto snapshot = snapshot_for();
    EXPECT_FALSE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    EXPECT_FALSE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    EXPECT_FALSE(era_split_communication_core_storage_reserve_responder_result(kGeneration));
}

TEST_F(EraSplitStoragePublicationRetire, UnclaimedInitiatorRequestAndReservationAreDiscarded) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));

    ASSERT_TRUE(era_split_communication_core_storage_retire_publications());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());
    EXPECT_FALSE(era_split_communication_core_storage_claim_initiator_request(&request));
    EXPECT_FALSE(era_split_communication_core_storage_reserve_initiator_result(kGeneration + 1U));
}

TEST_F(EraSplitStoragePublicationRetire, ReadyInitiatorResultIsDiscarded) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    ASSERT_TRUE(era_split_communication_core_storage_claim_initiator_request(&request));

    auto *result = era_split_communication_core_storage_begin_initiator_result(kGeneration);
    ASSERT_NE(result, nullptr);
    result->request_generation = kGeneration;
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_result(result));
    ASSERT_TRUE(era_split_communication_core_storage_initiator_result_ready());
    EXPECT_TRUE(era_split_communication_core_storage_result_due());

    ASSERT_TRUE(era_split_communication_core_storage_retire_publications());
    const era_split_communication_core_storage_initiator_result_t *view = nullptr;
    EXPECT_FALSE(era_split_communication_core_storage_initiator_result_ready());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());
    EXPECT_FALSE(era_split_communication_core_storage_acquire_initiator_result(&view));
    EXPECT_EQ(view, nullptr);
}

TEST_F(EraSplitStoragePublicationRetire, InitiatorResultCommitsEvenBeforeReadyAndReleasesSourceAfterReady) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    ASSERT_TRUE(era_split_communication_core_storage_claim_initiator_request(&request));
    auto *result = era_split_communication_core_storage_begin_initiator_result(kGeneration);
    ASSERT_NE(result, nullptr);
    result->request_generation = kGeneration;

    era_test_storage_watch_initiator_publish();
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_result(result));
    EXPECT_TRUE(era_test_storage_ready_observed());
    EXPECT_TRUE(era_test_storage_ready_seq_was_even());
    EXPECT_TRUE(era_test_storage_source_claim_was_held_at_ready());
}

TEST_F(EraSplitStoragePublicationRetire, ReadyResponderResultIsDiscarded) {
    auto snapshot = snapshot_for();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_reserve_responder_result(kGeneration));

    era_split_communication_core_storage_responder_result_t result{};
    result.snapshot_generation = kGeneration;
    result.request_fingerprint = 1U;
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_result(&result));
    ASSERT_TRUE(era_split_communication_core_storage_responder_result_ready());
    /* A ready responder result is globally due even before a later core0
       runtime-role/watch decision. The scheduler must wake on this ownership
       fact itself or the reserved result can permanently block all later
       storage responses during a relation transition. */
    EXPECT_TRUE(era_split_communication_core_storage_result_due());

    ASSERT_TRUE(era_split_communication_core_storage_retire_publications());
    EXPECT_FALSE(era_split_communication_core_storage_responder_result_ready());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());
    EXPECT_FALSE(era_split_communication_core_storage_drain_responder_result(&result));
}

TEST_F(EraSplitStoragePublicationRetire, UnclaimedResponderSnapshotIsDiscarded) {
    auto snapshot = snapshot_for();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));

    ASSERT_TRUE(era_split_communication_core_storage_retire_publications());
    EXPECT_FALSE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    snapshot.snapshot_generation++;
    EXPECT_FALSE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
}

TEST_F(EraSplitStoragePublicationRetire, ResponderResultCommitsEvenBeforeReadyAndReleasesSourceAfterReady) {
    auto snapshot = snapshot_for();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_reserve_responder_result(kGeneration));
    era_split_communication_core_storage_responder_result_t result{};
    result.snapshot_generation = kGeneration;
    result.request_fingerprint = 1U;

    era_test_storage_watch_responder_publish();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_result(&result));
    EXPECT_TRUE(era_test_storage_ready_observed());
    EXPECT_TRUE(era_test_storage_ready_seq_was_even());
    EXPECT_TRUE(era_test_storage_source_claim_was_held_at_ready());
}

TEST_F(EraSplitStoragePublicationRetire, ReadyInitiatorResultCanBeDiscardedWithoutRetiringSourceCapacity) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    ASSERT_TRUE(era_split_communication_core_storage_claim_initiator_request(&request));

    auto *result = era_split_communication_core_storage_begin_initiator_result(kGeneration);
    ASSERT_NE(result, nullptr);
    result->request_generation = kGeneration;
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_result(result));
    ASSERT_TRUE(era_split_communication_core_storage_result_due());

    EXPECT_TRUE(era_split_communication_core_storage_discard_ready_results());
    EXPECT_FALSE(era_split_communication_core_storage_initiator_result_ready());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());

    /* Relation loss is not CLEAN: discarding an old result must leave the
       source publication seat reusable for the next relation generation. */
    auto next = request_for(kGeneration + 1U);
    next.relation_generation++;
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration + 1U));
    EXPECT_TRUE(era_split_communication_core_storage_publish_initiator_request(&next, 25000U));
}

TEST_F(EraSplitStoragePublicationRetire, ReadyResponderResultDiscardReopensReplyCapacity) {
    auto snapshot = snapshot_for();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_reserve_responder_result(kGeneration));

    era_split_communication_core_storage_responder_result_t result{};
    result.snapshot_generation = kGeneration;
    result.request_fingerprint  = 1U;
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_result(&result));
    ASSERT_TRUE(era_split_communication_core_storage_result_due());

    EXPECT_TRUE(era_split_communication_core_storage_discard_ready_results());
    EXPECT_FALSE(era_split_communication_core_storage_responder_result_ready());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());

    auto next = snapshot_for(kGeneration + 1U);
    next.relation_generation++;
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&next));
    ASSERT_TRUE(era_split_communication_core_storage_claim_responder_snapshot(&next));
    EXPECT_TRUE(era_split_communication_core_storage_reserve_responder_result(kGeneration + 1U));
}

TEST_F(EraSplitStoragePublicationRetire, BothReadyResultsDiscardInOneCore0OwnershipPass) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    ASSERT_TRUE(era_split_communication_core_storage_claim_initiator_request(&request));
    auto *initiator_result = era_split_communication_core_storage_begin_initiator_result(kGeneration);
    ASSERT_NE(initiator_result, nullptr);
    initiator_result->request_generation = kGeneration;
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_result(initiator_result));

    auto snapshot = snapshot_for(kGeneration + 1U);
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_reserve_responder_result(kGeneration + 1U));
    era_split_communication_core_storage_responder_result_t responder_result{};
    responder_result.snapshot_generation = kGeneration + 1U;
    responder_result.request_fingerprint  = 1U;
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_result(&responder_result));
    ASSERT_TRUE(era_split_communication_core_storage_result_due());

    EXPECT_TRUE(era_split_communication_core_storage_discard_ready_results());
    EXPECT_FALSE(era_split_communication_core_storage_initiator_result_ready());
    EXPECT_FALSE(era_split_communication_core_storage_responder_result_ready());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());
}

TEST_F(EraSplitStoragePublicationRetire, InFlightInitiatorReservationSurvivesUntilResultBecomesReady) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    ASSERT_TRUE(era_split_communication_core_storage_claim_initiator_request(&request));
    auto *result = era_split_communication_core_storage_begin_initiator_result(kGeneration);
    ASSERT_NE(result, nullptr);
    result->request_generation = kGeneration;

    EXPECT_FALSE(era_split_communication_core_storage_result_due());
    EXPECT_FALSE(era_split_communication_core_storage_discard_ready_results());
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_result(result));
    ASSERT_TRUE(era_split_communication_core_storage_result_due());
    EXPECT_TRUE(era_split_communication_core_storage_discard_ready_results());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());
}

TEST_F(EraSplitStoragePublicationRetire, InFlightResponderReservationSurvivesUntilResultBecomesReady) {
    auto snapshot = snapshot_for();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_reserve_responder_result(kGeneration));
    era_split_communication_core_storage_responder_result_t result{};
    result.snapshot_generation = kGeneration;
    result.request_fingerprint  = 1U;

    EXPECT_FALSE(era_split_communication_core_storage_result_due());
    EXPECT_FALSE(era_split_communication_core_storage_discard_ready_results());
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_result(&result));
    ASSERT_TRUE(era_split_communication_core_storage_result_due());
    EXPECT_TRUE(era_split_communication_core_storage_discard_ready_results());
    EXPECT_FALSE(era_split_communication_core_storage_result_due());
}

TEST_F(EraSplitStoragePublicationRetire, ActiveInitiatorClaimBlocksThenPublishedResultDrainsAndRetryRetires) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    ASSERT_TRUE(era_split_communication_core_storage_claim_initiator_request(&request));
    EXPECT_FALSE(era_split_communication_core_storage_retire_publications());

    auto *result = era_split_communication_core_storage_begin_initiator_result(kGeneration);
    ASSERT_NE(result, nullptr);
    result->request_generation = kGeneration;
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_result(result));

    const era_split_communication_core_storage_initiator_result_t *view = nullptr;
    ASSERT_TRUE(era_split_communication_core_storage_acquire_initiator_result(&view));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(era_split_communication_core_storage_release_initiator_result(kGeneration));
    EXPECT_TRUE(era_split_communication_core_storage_retire_publications());
}

TEST_F(EraSplitStoragePublicationRetire, ActiveResponderClaimBlocksThenPublishedResultDrainsAndRetryRetires) {
    auto snapshot = snapshot_for();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_claim_responder_snapshot(&snapshot));
    ASSERT_TRUE(era_split_communication_core_storage_reserve_responder_result(kGeneration));
    EXPECT_FALSE(era_split_communication_core_storage_retire_publications());

    era_split_communication_core_storage_responder_result_t result{};
    result.snapshot_generation = kGeneration;
    result.request_fingerprint = 1U;
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_result(&result));
    ASSERT_TRUE(era_split_communication_core_storage_drain_responder_result(&result));
    EXPECT_TRUE(era_split_communication_core_storage_retire_publications());
}

TEST_F(EraSplitStoragePublicationRetire, LateInitiatorClaimRestoresSourceThenRetryRetires) {
    auto request = request_for();
    ASSERT_TRUE(era_split_communication_core_storage_reserve_initiator_result(kGeneration));
    ASSERT_TRUE(era_split_communication_core_storage_publish_initiator_request(&request, 25000U));
    const uint32_t source_seq = era_test_storage_initiator_publication_seq();

    era_test_storage_inject_late_initiator_claim();
    EXPECT_FALSE(era_split_communication_core_storage_retire_publications());
    EXPECT_TRUE(era_test_storage_late_claim_was_injected());
    EXPECT_EQ(era_test_storage_initiator_publication_seq(), source_seq);

    era_test_storage_release_late_claim();
    EXPECT_TRUE(era_split_communication_core_storage_retire_publications());
}

TEST_F(EraSplitStoragePublicationRetire, LateResponderClaimRestoresSourceThenRetryRetires) {
    auto snapshot = snapshot_for();
    ASSERT_TRUE(era_split_communication_core_storage_publish_responder_snapshot(&snapshot));
    const uint32_t source_seq = era_test_storage_responder_publication_seq();

    era_test_storage_inject_late_responder_claim();
    EXPECT_FALSE(era_split_communication_core_storage_retire_publications());
    EXPECT_TRUE(era_test_storage_late_claim_was_injected());
    EXPECT_EQ(era_test_storage_responder_publication_seq(), source_seq);

    era_test_storage_release_late_claim();
    EXPECT_TRUE(era_split_communication_core_storage_retire_publications());
}
