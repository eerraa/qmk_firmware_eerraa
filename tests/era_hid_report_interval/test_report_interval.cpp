// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

#include <cstring>

extern "C" {
#include "keyboards/era/common/system/era_hid_report_interval.h"
#include "platform_under_test.h"
}

namespace {

constexpr uint8_t KB = ERA_HID_REPORT_LANE_KEYBOARD;
constexpr uint8_t SH = ERA_HID_REPORT_LANE_SHARED;

struct Report {
    uint8_t bytes[8];
    explicit Report(uint8_t tag) {
        std::memset(bytes, 0, sizeof(bytes));
        bytes[2] = tag;
    }
};

bool send(uint8_t lane, uint8_t tag) {
    Report r(tag);
    return era_test_send_keyboard(lane, r.bytes, sizeof(r.bytes));
}

uint8_t posted_tag(unsigned index) {
    return era_test_posts[index].data[2];
}

era_hid_report_interval_diagnostics_t diag() {
    era_hid_report_interval_diagnostics_t d{};
    era_hid_report_interval_get_diagnostics(&d);
    return d;
}

class EraHidReportInterval : public ::testing::Test {
   protected:
    void SetUp() override {
        era_test_sink_reset();
        era_hid_report_interval_init(1); // the test clock is milliseconds
    }
};

} // namespace

TEST_F(EraHidReportInterval, IdleReportsPostDirectlyAndKeepNoState) {
    EXPECT_TRUE(send(KB, 1));
    EXPECT_EQ(era_test_post_count, 1U);
    EXPECT_FALSE(era_hid_report_interval_active());
    era_hid_report_interval_task(5);
    EXPECT_EQ(era_test_post_count, 1U);
}

TEST_F(EraHidReportInterval, ReleaseWaitsForThePressToCompletePlusTheWidth) {
    ASSERT_TRUE(send(KB, 0x39)); // Caps down leaves at once
    era_hid_report_interval_request(80, 0);
    EXPECT_TRUE(era_hid_report_interval_active());
    EXPECT_FALSE(send(KB, 0)); // the release is held, logically done
    EXPECT_EQ(era_test_post_count, 1U);
    EXPECT_EQ(diag().backlog, 1U);

    era_hid_report_interval_task(3);
    EXPECT_EQ(era_test_post_count, 1U);
    era_hid_report_interval_note_completed(KB, 5); // the host took the press
    era_hid_report_interval_task(6);
    era_hid_report_interval_task(84);
    EXPECT_EQ(era_test_post_count, 1U);
    era_hid_report_interval_task(85);
    ASSERT_EQ(era_test_post_count, 2U);
    EXPECT_EQ(posted_tag(1), 0U);
    EXPECT_FALSE(era_hid_report_interval_active());
}

TEST_F(EraHidReportInterval, RepeatedRequestsTakeTheLargerWidth) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_request(80, 0);
    era_hid_report_interval_request(200, 0);
    EXPECT_FALSE(send(KB, 0));
    era_hid_report_interval_note_completed(KB, 5);
    era_hid_report_interval_task(6);
    era_hid_report_interval_task(204);
    EXPECT_EQ(era_test_post_count, 1U);
    era_hid_report_interval_task(205);
    EXPECT_EQ(era_test_post_count, 2U);
}

TEST_F(EraHidReportInterval, ElapsedTimeSinceCompletionCountsTowardTheWidth) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_note_completed(KB, 0);
    era_hid_report_interval_request(80, 50);
    EXPECT_FALSE(send(KB, 0));
    era_hid_report_interval_task(79);
    EXPECT_EQ(era_test_post_count, 1U);
    era_hid_report_interval_task(80);
    EXPECT_EQ(era_test_post_count, 2U);

    // Long enough after the completion the width is already true: no hold.
    era_hid_report_interval_note_completed(KB, 81); // the release reached the host
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_note_completed(KB, 100);
    era_hid_report_interval_request(80, 200);
    EXPECT_FALSE(era_hid_report_interval_active());
    EXPECT_TRUE(send(KB, 0));
    EXPECT_EQ(era_test_post_count, 4U);
}

TEST_F(EraHidReportInterval, ZeroWidthIsNoRequest) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_request(0, 0);
    EXPECT_FALSE(era_hid_report_interval_active());
    EXPECT_TRUE(send(KB, 0));
}

TEST_F(EraHidReportInterval, HeldReportsKeepOrderAndCarryTheirOwnWidths) {
    ASSERT_TRUE(send(KB, 0x39)); // post 1: Caps down
    era_hid_report_interval_request(80, 0);
    era_hid_report_interval_note_completed(KB, 2);
    era_hid_report_interval_task(3); // deadline 82

    EXPECT_FALSE(send(KB, 0x04)); // A down, held
    EXPECT_FALSE(send(KB, 0x39)); // a second Caps tap: its down is held too
    era_hid_report_interval_request(80, 10); // and its width rides with that entry
    EXPECT_FALSE(send(KB, 0));    // its release waits behind
    EXPECT_EQ(diag().backlog, 3U);

    era_hid_report_interval_task(82);
    ASSERT_EQ(era_test_post_count, 3U); // A down, Caps down; the release stays behind the new width
    EXPECT_EQ(posted_tag(1), 0x04U);
    EXPECT_EQ(posted_tag(2), 0x39U);
    EXPECT_TRUE(diag().gate_closed);

    era_hid_report_interval_note_completed(KB, 83); // A down reached the host
    era_hid_report_interval_note_completed(KB, 84); // Caps down reached the host
    era_hid_report_interval_task(85);
    era_hid_report_interval_task(163);
    EXPECT_EQ(era_test_post_count, 3U);
    era_hid_report_interval_task(164);
    ASSERT_EQ(era_test_post_count, 4U);
    EXPECT_EQ(posted_tag(3), 0U);
    EXPECT_FALSE(era_hid_report_interval_active());
}

TEST_F(EraHidReportInterval, AFullBacklogFoldsTheNewestReportIntoItsTail) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_request(80, 0); // in flight: the gate is closed until the completion arrives
    for (uint8_t tag = 1; tag <= ERA_HID_REPORT_INTERVAL_BACKLOG; tag++) {
        EXPECT_FALSE(send(KB, tag));
    }
    era_hid_report_interval_request(80, 1); // the tail's own width
    EXPECT_FALSE(send(KB, 0x40));         // one more than fits: the tail becomes this state
    auto d = diag();
    EXPECT_EQ(d.backlog, ERA_HID_REPORT_INTERVAL_BACKLOG);
    EXPECT_EQ(d.coalesced, 1U);

    era_hid_report_interval_note_completed(KB, 5);
    era_hid_report_interval_task(85);
    ASSERT_EQ(era_test_post_count, 1U + ERA_HID_REPORT_INTERVAL_BACKLOG);
    for (uint8_t tag = 1; tag < ERA_HID_REPORT_INTERVAL_BACKLOG; tag++) {
        EXPECT_EQ(posted_tag(tag), tag);
    }
    EXPECT_EQ(posted_tag(ERA_HID_REPORT_INTERVAL_BACKLOG), 0x40U);
    EXPECT_TRUE(diag().gate_closed); // the folded tail kept the width it carried
}

TEST_F(EraHidReportInterval, OtherReportsOnTheSharedLaneCountForCompletionOrder) {
    ASSERT_TRUE(send(SH, 0x39));          // NKRO Caps down: shared post 1
    uint8_t mouse[5] = {2, 0, 3, 0, 0};
    era_test_send_other(SH, mouse, 5);    // a mouse report behind it: shared post 2
    era_hid_report_interval_request(80, 0);
    EXPECT_FALSE(send(SH, 0));
    era_hid_report_interval_note_completed(SH, 5); // post 1 reached the host; post 2 has not
    era_hid_report_interval_task(6);
    era_hid_report_interval_task(84);
    EXPECT_EQ(era_test_post_count, 2U);
    era_hid_report_interval_task(85);
    ASSERT_EQ(era_test_post_count, 3U);
    EXPECT_EQ(era_test_posts[2].lane, SH);
}

TEST_F(EraHidReportInterval, AnAnchorWithoutACompletionOpensAfterTheLimit) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_request(80, 0);
    EXPECT_FALSE(send(KB, 0));
    era_hid_report_interval_task(ERA_HID_REPORT_INTERVAL_ANCHOR_LIMIT_MS - 1);
    EXPECT_EQ(era_test_post_count, 1U);
    era_hid_report_interval_task(ERA_HID_REPORT_INTERVAL_ANCHOR_LIMIT_MS);
    EXPECT_EQ(era_test_post_count, 2U);
    EXPECT_EQ(diag().valve_trips, 1U);
    EXPECT_FALSE(era_hid_report_interval_active());
}

TEST_F(EraHidReportInterval, ResetDropsTheBacklogAndTheWidth) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_request(80, 0);
    EXPECT_FALSE(send(KB, 0));
    era_hid_report_interval_note_session_edge(false);
    EXPECT_TRUE(era_hid_report_interval_active());
    era_hid_report_interval_task(1);
    EXPECT_EQ(era_test_post_count, 1U);
    EXPECT_FALSE(era_hid_report_interval_active());
    EXPECT_TRUE(send(KB, 0x04));
    EXPECT_EQ(era_test_post_count, 2U);
}

TEST_F(EraHidReportInterval, SuspendKeepsTheBacklogAndMeasuresTheWidthFromResume) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_request(80, 0);
    EXPECT_FALSE(send(KB, 0));
    era_test_active = false;
    era_hid_report_interval_note_session_edge(true);
    era_hid_report_interval_task(10); // the pending completion will never come: width from now
    EXPECT_EQ(diag().backlog, 1U);
    era_hid_report_interval_task(90); // open, but the transport is asleep
    EXPECT_EQ(era_test_post_count, 1U);
    EXPECT_EQ(era_test_refused, 1U);
    era_test_active = true;
    era_hid_report_interval_task(91);
    ASSERT_EQ(era_test_post_count, 2U);
    EXPECT_EQ(posted_tag(1), 0U);
}

TEST_F(EraHidReportInterval, NoRoomDefersTheDrainWithoutReordering) {
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_note_completed(KB, 0);
    era_hid_report_interval_request(80, 0);
    EXPECT_FALSE(send(KB, 0x04));
    EXPECT_FALSE(send(KB, 0x05));
    era_test_room = false;
    era_hid_report_interval_task(80);
    EXPECT_EQ(era_test_post_count, 1U);
    EXPECT_EQ(diag().backlog, 2U);
    era_test_room = true;
    era_hid_report_interval_task(81);
    ASSERT_EQ(era_test_post_count, 3U);
    EXPECT_EQ(posted_tag(1), 0x04U);
    EXPECT_EQ(posted_tag(2), 0x05U);
}

TEST_F(EraHidReportInterval, ARequestWithNothingPostedHoldsNothing) {
    era_hid_report_interval_request(80, 0);
    EXPECT_FALSE(era_hid_report_interval_active());
    EXPECT_TRUE(send(KB, 0x04));
}

TEST_F(EraHidReportInterval, TheClockUnitAndTheWidthCapAreHonoured) {
    era_hid_report_interval_init(1000); // microsecond ticks
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_note_completed(KB, 0);
    era_hid_report_interval_request(80, 0);
    EXPECT_FALSE(send(KB, 0));
    era_hid_report_interval_task(79999);
    EXPECT_EQ(era_test_post_count, 1U);
    era_hid_report_interval_task(80000);
    EXPECT_EQ(era_test_post_count, 2U);

    era_hid_report_interval_note_completed(KB, 80001); // the release reached the host
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_note_completed(KB, 100000);
    era_hid_report_interval_request(9000, 100000); // capped at the maximum
    EXPECT_FALSE(send(KB, 0));
    era_hid_report_interval_task(100000 + ERA_HID_REPORT_INTERVAL_REQUEST_MAX_MS * 1000 - 1);
    EXPECT_EQ(era_test_post_count, 3U);
    era_hid_report_interval_task(100000 + ERA_HID_REPORT_INTERVAL_REQUEST_MAX_MS * 1000);
    EXPECT_EQ(era_test_post_count, 4U);
}

TEST_F(EraHidReportInterval, WrapOfTheClockDoesNotBreakTheWidth) {
    const uint32_t start = 0xFFFFFFF0U;
    ASSERT_TRUE(send(KB, 0x39));
    era_hid_report_interval_note_completed(KB, start);
    era_hid_report_interval_request(80, start);
    EXPECT_FALSE(send(KB, 0));
    era_hid_report_interval_task(start + 79);
    EXPECT_EQ(era_test_post_count, 1U);
    era_hid_report_interval_task(start + 80); // past the wrap
    EXPECT_EQ(era_test_post_count, 2U);
}
