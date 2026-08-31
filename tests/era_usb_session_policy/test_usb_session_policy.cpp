// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/system/era_usb_session_policy.h"
}

TEST(EraUsbSessionPolicy, PendingSofIsArrivalEvidenceAtAnyAge) {
    EXPECT_FALSE(era_usb_session_policy_isr_owned_frames_lost(500000U, 500000U, true, 300000U));
}

TEST(EraUsbSessionPolicy, OwnershipGapMustReachFullThreshold) {
    EXPECT_FALSE(era_usb_session_policy_isr_owned_frames_lost(299999U, 500000U, false, 300000U));
    EXPECT_TRUE(era_usb_session_policy_isr_owned_frames_lost(300000U, 500000U, false, 300000U));
}

TEST(EraUsbSessionPolicy, LastObservedFrameMustAlsoBeStale) {
    EXPECT_FALSE(era_usb_session_policy_isr_owned_frames_lost(500000U, 299999U, false, 300000U));
    EXPECT_TRUE(era_usb_session_policy_isr_owned_frames_lost(500000U, 300000U, false, 300000U));
}
