// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gtest/gtest.h"
extern "C" {
#include "standing_under_test.h"
}
class EraSplitStandingLifecycle : public testing::Test {
protected:
    void SetUp() override { era_test_standing_reset(); }
};
TEST_F(EraSplitStandingLifecycle, RevocationCannotWriteCore1OwnedState) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(true, 0x87, true);
    ASSERT_TRUE(era_test_standing_run(1));
    const auto changes = era_test_standing_change_seq();
    era_test_standing_revoke();
    EXPECT_EQ(era_test_standing_relation(), 1);
    EXPECT_TRUE(era_test_standing_news_valid());
    EXPECT_EQ(era_test_standing_news(), 0x87);
    EXPECT_EQ(era_test_standing_change_seq(), changes);
    EXPECT_FALSE(era_test_standing_run(1));
}
TEST_F(EraSplitStandingLifecycle, LateOldResultNeverBecomesNewRelationsCache) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(true, 0x87, true);
    era_test_standing_rotate_during_transaction(1, 2);
    ASSERT_TRUE(era_test_standing_run(1));
    ASSERT_EQ(era_test_standing_relation(), 1); // safely rejected by Core0
    const auto changes = era_test_standing_change_seq();
    era_test_standing_reply(false, 0, true);    // sparse new-relation ACK
    ASSERT_TRUE(era_test_standing_run(1));
    EXPECT_EQ(era_test_standing_relation(), 2);
    EXPECT_FALSE(era_test_standing_news_valid());
    EXPECT_GT(era_test_standing_change_seq(), changes);
    EXPECT_EQ(era_test_standing_exchanges(), 2u);
}
TEST_F(EraSplitStandingLifecycle, SameValueFirstDeliveredInNewRelationStillWakes) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(true, 7, true);
    era_test_standing_rotate_during_transaction(1, 2);
    ASSERT_TRUE(era_test_standing_run(1));
    const auto changes = era_test_standing_change_seq();
    ASSERT_TRUE(era_test_standing_run(1));
    EXPECT_EQ(era_test_standing_relation(), 2);
    EXPECT_GT(era_test_standing_change_seq(), changes);
    EXPECT_EQ(era_test_standing_news(), 7);
}
TEST_F(EraSplitStandingLifecycle, OwnerEpochChangeAlsoRetiresSentAndReceivedState) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(true, 0x87, true);
    ASSERT_TRUE(era_test_standing_run(1));
    era_test_standing_publish(2, 1, false, true); // link rebuild, same relation
    era_test_standing_reply(false, 0, true);
    ASSERT_TRUE(era_test_standing_run(2));
    EXPECT_NE(era_test_standing_last_sections(), 0);
    EXPECT_FALSE(era_test_standing_news_valid());
}
TEST_F(EraSplitStandingLifecycle, ANewDisplayPlanIsNotSuccessfulSessionRevalidation) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(false, 0, false);
    ASSERT_TRUE(era_test_standing_run(1));
    ASSERT_TRUE(era_test_standing_stopped());
    era_test_standing_reply(false, 0, true);
    era_test_standing_publish(1, 1, true, true);
    EXPECT_FALSE(era_test_standing_run(1));
    era_test_standing_publish(1, 1, false, false);
    EXPECT_FALSE(era_test_standing_run(1));
    EXPECT_EQ(era_test_standing_transactions(), 1u);
}
TEST_F(EraSplitStandingLifecycle, UnchangedCurrentRelationDoesNotWakeAtPollRate) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(true, 7, true);
    ASSERT_TRUE(era_test_standing_run(1));
    const auto changes = era_test_standing_change_seq();
    for (unsigned i = 0; i < 50; ++i) ASSERT_TRUE(era_test_standing_run(1));
    EXPECT_EQ(era_test_standing_change_seq(), changes);
    EXPECT_EQ(era_test_standing_exchanges(), 51u);
}

TEST_F(EraSplitStandingLifecycle, SuccessfulSessionForExactStopResumes) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(false, 0, false);
    ASSERT_TRUE(era_test_standing_run(1));
    const auto stop = era_test_standing_capture_stop(1, 1);
    ASSERT_NE(stop, 0);
    era_test_standing_resume(1, 1, stop);
    era_test_standing_reply(false, 0, true);
    EXPECT_TRUE(era_test_standing_run(1));
    EXPECT_FALSE(era_test_standing_stopped());
    EXPECT_TRUE(era_test_standing_snapshot_consistent());
}
TEST_F(EraSplitStandingLifecycle, SessionIssuedBeforeFailureCannotReleaseIt) {
    era_test_standing_publish(1, 1, false, true);
    const auto earlier_session = era_test_standing_capture_stop(1, 1);
    ASSERT_EQ(earlier_session, 0);
    era_test_standing_reply(false, 0, false);
    ASSERT_TRUE(era_test_standing_run(1));
    const auto consumed_stop_wake = era_test_standing_change_seq();
    // The false return tells the SESSION result owner to keep revalidation
    // pending: there will be no second stop wake to rescue a lost request.
    EXPECT_FALSE(era_test_standing_resume(1, 1, earlier_session));
    EXPECT_FALSE(era_test_standing_run(1));
    EXPECT_EQ(era_test_standing_change_seq(), consumed_stop_wake);
    const auto fresh_session = era_test_standing_capture_stop(1, 1);
    EXPECT_TRUE(era_test_standing_resume(1, 1, fresh_session));
    era_test_standing_reply(false, 0, true);
    EXPECT_TRUE(era_test_standing_run(1));
}
TEST_F(EraSplitStandingLifecycle, FailedRecoveryPublishesNewStopAndRejectsOldAcknowledgement) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(false, 0, false);
    ASSERT_TRUE(era_test_standing_run(1));
    const auto first = era_test_standing_capture_stop(1, 1);
    const auto changes = era_test_standing_change_seq();
    era_test_standing_resume(1, 1, first);
    ASSERT_TRUE(era_test_standing_run(1)); // recovery also fails
    const auto second = era_test_standing_capture_stop(1, 1);
    EXPECT_NE(second, first);
    EXPECT_GT(era_test_standing_change_seq(), changes);
    era_test_standing_resume(1, 1, first);
    EXPECT_FALSE(era_test_standing_run(1));
    era_test_standing_resume(1, 1, second);
    era_test_standing_reply(false, 0, true);
    EXPECT_TRUE(era_test_standing_run(1));
}
TEST_F(EraSplitStandingLifecycle, AcknowledgementCannotCrossOwnerOrRelation) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(false, 0, false);
    ASSERT_TRUE(era_test_standing_run(1));
    const auto stop = era_test_standing_capture_stop(1, 1);
    EXPECT_EQ(era_test_standing_capture_stop(2, 1), 0);
    EXPECT_EQ(era_test_standing_capture_stop(1, 2), 0);
    era_test_standing_resume(2, 1, stop);
    era_test_standing_resume(1, 2, stop);
    EXPECT_FALSE(era_test_standing_run(1));
}

TEST_F(EraSplitStandingLifecycle, OwnerOnlyRestartCannotAliasTheAppliedVisualReceipt) {
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_visual_reply();
    ASSERT_TRUE(era_test_standing_run(1));
    const auto applied = era_test_standing_visual_seq();
    era_test_standing_publish(2, 1, false, true);
    ASSERT_TRUE(era_test_standing_run(2));
    EXPECT_NE(era_test_standing_visual_seq(), applied);
}

TEST_F(EraSplitStandingLifecycle, WithheldGrantDoesNotInventSessionRevalidationDebt) {
    EXPECT_TRUE(era_test_standing_resume(1, 1, 0));
    era_test_standing_publish(1, 1, false, true);
    era_test_standing_reply(false, 0, false);
    ASSERT_TRUE(era_test_standing_run(1));
    era_test_standing_revoke();
    EXPECT_TRUE(era_test_standing_resume(1, 1, 0));
    EXPECT_FALSE(era_test_standing_run(1));
}
