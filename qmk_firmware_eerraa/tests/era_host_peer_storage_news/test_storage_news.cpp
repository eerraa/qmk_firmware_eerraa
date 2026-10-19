// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gtest/gtest.h"
extern "C" {
#include "storage_under_test.h"
#include "timer.h"
#include "wait.h"
}

class EraHostPeerStorageNews : public testing::Test {
protected:
    void SetUp() override {
        timer_clear();
        era_test_news_reset();
    }
};

TEST_F(EraHostPeerStorageNews, NewsReceivedBeforeAuditMustNotReplayAfterAudit) {
    era_test_news_rotate_relation();
    // Scheduler order: standing-state drain, then storage runtime/audit start.
    era_test_news_receive(7);
    era_test_news_begin_audit();
    EXPECT_FALSE(era_test_news_pending());
    era_test_news_finish_unchanged_audit();
    EXPECT_FALSE(era_test_news_pending());

    // An unrelated standing edge re-hands every valid cached field.
    era_test_news_receive(7);
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_pending());
    EXPECT_FALSE(era_test_news_visible());

    // Even a summary that immediately finds no work must not leave a red floor.
    era_test_news_finish_unchanged_audit();
    wait_ms(1);
    EXPECT_FALSE(era_test_news_visible());
    wait_ms(158);
    EXPECT_FALSE(era_test_news_visible());
    wait_ms(1);
    EXPECT_FALSE(era_test_news_visible());
}

TEST_F(EraHostPeerStorageNews, NewsReceivedDuringAuditRemainsDeduplicated) {
    era_test_news_begin_audit();
    era_test_news_receive(7);
    era_test_news_finish_unchanged_audit();
    era_test_news_receive(7);
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_visible());
}

TEST_F(EraHostPeerStorageNews, PolicyOnlyAuditMustNotForgetAlreadyConsumedNews) {
    era_test_news_receive(7);
    era_test_news_finish_unchanged_audit();
    era_test_news_begin_audit();
    era_test_news_finish_unchanged_audit();
    era_test_news_receive(7);
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_visible());
}

TEST_F(EraHostPeerStorageNews, UnchangedFreshDefaultsStayDark) {
    era_test_news_receive(0);
    era_test_news_begin_audit();
    era_test_news_finish_unchanged_audit();
    for (unsigned i = 0; i < 32; ++i) {
        era_test_news_receive(0);
        EXPECT_FALSE(era_test_news_summary_pending());
        EXPECT_FALSE(era_test_news_visible());
        wait_ms(20);
    }
}

TEST_F(EraHostPeerStorageNews, GenuineNewsStillArmsSummaryAndMinimumVisibleFloor) {
    era_test_news_begin_audit();
    era_test_news_receive(7);
    era_test_news_finish_unchanged_audit();
    era_test_news_receive(8);
    EXPECT_TRUE(era_test_news_summary_pending());
    EXPECT_TRUE(era_test_news_pending());
    EXPECT_TRUE(era_test_news_visible());
    era_test_news_finish_unchanged_audit();
    wait_ms(159);
    EXPECT_TRUE(era_test_news_visible());
    wait_ms(1);
    EXPECT_FALSE(era_test_news_visible());
}

TEST_F(EraHostPeerStorageNews, PendingFlagDoesNotInventAnotherNewsValue) {
    era_test_news_begin_audit();
    era_test_news_receive(7);
    era_test_news_finish_unchanged_audit();
    era_test_news_receive(0x87);
    EXPECT_FALSE(era_test_news_summary_pending());
    era_test_news_receive(7);
    EXPECT_FALSE(era_test_news_summary_pending());
}

TEST_F(EraHostPeerStorageNews, NewRelationCanReuseThePreviousPeersCounter) {
    era_test_news_receive(7);
    era_test_news_finish_unchanged_audit();
    era_test_news_receive(7);
    ASSERT_FALSE(era_test_news_summary_pending());

    // The reopened relation owes its audit; the reused value is that audit's to
    // classify and arms no visible in-session summary ahead of it.
    era_test_news_rotate_relation();
    era_test_news_receive(7);
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_pending());
    EXPECT_FALSE(era_test_news_visible());
    EXPECT_TRUE(era_test_news_audit_if_due(2, 1));
    EXPECT_TRUE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_pending());
    era_test_news_finish_unchanged_audit();
    era_test_news_receive(7);
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_visible());
    // Genuine news after that audit is in-session news again.
    era_test_news_receive(8);
    EXPECT_TRUE(era_test_news_summary_pending());
    EXPECT_TRUE(era_test_news_pending());
}

TEST_F(EraHostPeerStorageNews, NewsBeforeADeferredReopenAuditNeverLightsThePanel) {
    era_test_news_receive(7);
    era_test_news_finish_unchanged_audit();
    era_test_news_rotate_relation();
    // The audit may wait behind a link agreement for many passes; the news
    // consumed meanwhile must stay dark on both halves for all of them.
    for (unsigned i = 0; i < 20; ++i) {
        era_test_news_receive(7);
        EXPECT_FALSE(era_test_news_summary_pending());
        EXPECT_FALSE(era_test_news_visible());
        wait_ms(10);
    }
    EXPECT_TRUE(era_test_news_audit_if_due(2, 1));
    EXPECT_FALSE(era_test_news_visible());
}

TEST_F(EraHostPeerStorageNews, RotationPreservesLocalNewsAndConfirmedPairObligations) {
    era_test_news_seed_pair_obligations();
    ASSERT_TRUE(era_test_news_pending());
    era_test_news_rotate_relation();
    EXPECT_EQ(era_test_news_local_value(), 9);
    EXPECT_TRUE(era_test_news_pending());
    EXPECT_TRUE(era_test_news_visible());
}

TEST_F(EraHostPeerStorageNews, InFlightSummaryRemainsVisibleWithoutOwningNewRequests) {
    era_test_news_receive(7);
    era_test_news_start_summary();
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_TRUE(era_test_news_pending());
    EXPECT_TRUE(era_test_news_visible());
    era_test_news_receive(8);
    era_test_news_complete_summary(0, 0);
    EXPECT_TRUE(era_test_news_summary_pending());
    EXPECT_TRUE(era_test_news_pending());
    era_test_news_start_summary();
    era_test_news_complete_summary(0, 0);
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_pending());
    era_test_news_receive(8);
    EXPECT_FALSE(era_test_news_summary_pending());
}
TEST_F(EraHostPeerStorageNews, InFlightRelationAuditRemainsProvisional) {
    era_test_news_begin_audit();
    era_test_news_start_summary();
    EXPECT_FALSE(era_test_news_pending());
    EXPECT_FALSE(era_test_news_visible());
    era_test_news_complete_summary(0, 0);
    EXPECT_EQ(era_test_news_probe_mask(), 0x7f);
    EXPECT_FALSE(era_test_news_visible());
}
TEST_F(EraHostPeerStorageNews, RecoverableSummaryAbortRestoresInboxRequest) {
    era_test_news_receive(7);
    era_test_news_start_summary();
    era_test_news_abort_summary(false);
    EXPECT_TRUE(era_test_news_summary_pending());
}
TEST_F(EraHostPeerStorageNews, TerminalSummaryRefusalDoesNotRetryForever) {
    era_test_news_receive(7);
    era_test_news_start_summary();
    era_test_news_abort_summary(true);
    EXPECT_FALSE(era_test_news_summary_pending());
    EXPECT_FALSE(era_test_news_pending());
}
TEST_F(EraHostPeerStorageNews, NewNewsSupersedesUnsentDirection) {
    era_test_news_seed_direction();
    ASSERT_TRUE(era_test_news_idle_token());
    era_test_news_receive(7);
    EXPECT_FALSE(era_test_news_idle_token());
    EXPECT_TRUE(era_test_news_summary_pending());
}
TEST_F(EraHostPeerStorageNews, SummaryReplacesRatherThanUnionsObsoleteDirections) {
    era_test_news_seed_direction();
    era_test_news_receive(7);
    era_test_news_start_summary();
    era_test_news_complete_summary(0, 1);
    EXPECT_EQ(era_test_news_probe_mask(), 1);
    EXPECT_EQ(era_test_news_push_mask(), 0);
    EXPECT_EQ(era_test_news_conflict_mask(), 0);
}
TEST_F(EraHostPeerStorageNews, SourceChangedDeferReturnsThroughArbitration) {
    EXPECT_TRUE(era_test_news_select_deferred_summary());
    EXPECT_TRUE(era_test_news_summary_pending());
}
TEST_F(EraHostPeerStorageNews, TransactionAdoptionCannotPretendTheNewRelationWasAudited) {
    ASSERT_TRUE(era_test_news_audit_if_due(1, 1));
    era_test_news_finish_unchanged_audit();
    era_test_news_adopt_transaction_identity(2, 1);
    EXPECT_TRUE(era_test_news_audit_if_due(2, 1));
    era_test_news_finish_unchanged_audit();
    EXPECT_FALSE(era_test_news_audit_if_due(2, 1));
    EXPECT_TRUE(era_test_news_audit_if_due(2, 2));
}

TEST_F(EraHostPeerStorageNews, StaleResultsCannotRetireTheCurrentRequest) {
    era_test_news_pending_request();
    for (uint8_t field = 1; field <= 7; ++field) {
        EXPECT_FALSE(era_test_news_take_result(field));
        EXPECT_TRUE(era_test_news_request_pending());
    }
    EXPECT_TRUE(era_test_news_take_result(0));
    EXPECT_FALSE(era_test_news_request_pending());
    EXPECT_FALSE(era_test_news_take_result(0));
}
TEST_F(EraHostPeerStorageNews, TerminalSummaryCannotReleaseAnObsoleteDirection) {
    era_test_news_seed_direction();
    era_test_news_receive(7);
    era_test_news_start_summary();
    era_test_news_abort_summary(true);
    EXPECT_EQ(era_test_news_probe_mask(), 0);
    EXPECT_EQ(era_test_news_push_mask(), 0);
    EXPECT_EQ(era_test_news_conflict_mask(), 0);
}

TEST_F(EraHostPeerStorageNews, QueuedBootAuditDoesNotDeadlockLinkButAdmittedAuditMustDrain) {
    EXPECT_FALSE(era_test_news_restart_wait());
    era_test_news_begin_audit();
    EXPECT_FALSE(era_test_news_restart_wait());
    era_test_news_start_summary();
    EXPECT_TRUE(era_test_news_restart_wait());
    era_test_news_close_indicator_gate();
    EXPECT_TRUE(era_test_news_restart_wait());
}
