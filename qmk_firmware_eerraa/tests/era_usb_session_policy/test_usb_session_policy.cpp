// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/system/era_usb_session_policy.h"
#include "keyboards/era/common/system/era_usb_session.h"
#include "sampler_under_test.h"
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

TEST(EraUsbSessionSampler, RemoteWakeOwnershipCannotBecomeFreshAtRawTimerWrap) {
    era_test_usb_sampler_reset(true);
    uint32_t age_ms = 0;
    ASSERT_TRUE(era_test_usb_sampler_observe(1000U, 7U, false, false, &age_ms));
    ASSERT_TRUE(era_test_usb_sampler_observe(2000U, 7U, true, false, &age_ms));
    ASSERT_FALSE(era_usb_session_frames_lost());

    // Continuous observations shorter than the 300 ms stale window, through
    // two complete raw-timer wraps. The ISR keeps ownership but no SOF arrives.
    // Neither lighting sleep nor the authority reducer's <10 ms freshness
    // input may revert simply because a timestamp becomes numerically equal.
    constexpr uint64_t wrap = uint64_t{1} << 32;
    uint64_t elapsed = 500000U;
    for (uint64_t cycle = 1U; cycle <= 2U; ++cycle) {
        for (; elapsed < cycle * wrap; elapsed += 250000U) {
            ASSERT_TRUE(era_test_usb_sampler_observe(static_cast<uint32_t>(2000U + elapsed), 7U, true, false, &age_ms));
            ASSERT_GE(age_ms, 300U) << "elapsed_us=" << elapsed;
            ASSERT_TRUE(era_usb_session_frames_lost()) << "elapsed_us=" << elapsed;
        }
        // Exactly the original ownership timestamp: an uncapped frame stamp
        // reports 1 ms here, which would also pass the host-authority gate.
        ASSERT_TRUE(era_test_usb_sampler_observe(2000U, 7U, true, false, &age_ms));
        ASSERT_GE(age_ms, 300U);
        ASSERT_TRUE(era_usb_session_frames_lost());
    }
}

TEST(EraUsbSessionSampler, SaturationCannotCreateUnseenSentinelAndHandoffReearnsEvidence) {
    era_test_usb_sampler_reset(true);
    uint32_t age_ms = 0;
    constexpr uint32_t start = UINT32_MAX - 999999U;
    ASSERT_TRUE(era_test_usb_sampler_observe(start, 7U, false, false, &age_ms));
    ASSERT_TRUE(era_test_usb_sampler_observe(start + 1000U, 7U, true, false, &age_ms));
    for (uint32_t elapsed = 200000U; elapsed <= 1600000U; elapsed += 200000U) {
        ASSERT_TRUE(era_test_usb_sampler_observe(start + elapsed, 7U, true, false, &age_ms));
        if (elapsed >= 400000U) {
            ASSERT_GE(age_ms, 300U);
            ASSERT_TRUE(era_usb_session_frames_lost()) << "elapsed_us=" << elapsed;
        }
    }
    // now == 600000, so the saturation target is numerically zero. Ownership
    // is still real, and the unread interval must not appear fresh on release.
    ASSERT_TRUE(era_test_usb_sampler_observe(601000U, 7U, false, false, &age_ms));
    EXPECT_EQ(age_ms, 0U);
    EXPECT_FALSE(era_usb_session_frames_lost());
    ASSERT_TRUE(era_test_usb_sampler_observe(801000U, 7U, false, false, &age_ms));
    ASSERT_TRUE(era_test_usb_sampler_observe(901000U, 7U, false, false, &age_ms));
    EXPECT_TRUE(era_usb_session_frames_lost());
}

TEST(EraUsbSessionSampler, PollingOwnedAbsenceStaysStaleAcrossRawWrap) {
    era_test_usb_sampler_reset(true);
    uint32_t age_ms = 0;
    constexpr uint32_t start = UINT32_MAX - 999999U;
    ASSERT_TRUE(era_test_usb_sampler_observe(start, 7U, false, false, &age_ms));
    for (uint32_t elapsed = 200000U; elapsed <= 2000000U; elapsed += 200000U) {
        ASSERT_TRUE(era_test_usb_sampler_observe(start + elapsed, 7U, false, false, &age_ms));
        if (elapsed >= 400000U) {
            ASSERT_GE(age_ms, 300U);
            ASSERT_TRUE(era_usb_session_frames_lost());
        }
    }
}

TEST(EraUsbSessionSampler, PendingFrameAndFreshCounterStillRecoverAfterLongOwnership) {
    era_test_usb_sampler_reset(true);
    uint32_t age_ms = 0;
    ASSERT_TRUE(era_test_usb_sampler_observe(1000U, 7U, false, false, &age_ms));
    ASSERT_TRUE(era_test_usb_sampler_observe(2000U, 7U, true, false, &age_ms));
    ASSERT_TRUE(era_test_usb_sampler_observe(400000U, 8U, true, false, &age_ms));
    EXPECT_TRUE(era_usb_session_frames_lost());
    EXPECT_EQ(era_test_usb_sampler_observed_count(), 7U);
    ASSERT_TRUE(era_test_usb_sampler_observe(401000U, 8U, true, true, &age_ms));
    EXPECT_FALSE(era_usb_session_frames_lost());
    EXPECT_EQ(era_test_usb_sampler_observed_count(), 7U);
    ASSERT_TRUE(era_test_usb_sampler_observe(402000U, 8U, false, false, &age_ms));
    EXPECT_EQ(era_test_usb_sampler_observed_count(), 8U);
    EXPECT_EQ(age_ms, 0U);
    EXPECT_FALSE(era_usb_session_frames_lost());
}

TEST(EraUsbSessionSampler, EqualCounterAfterUnobservedWindowIsNotFrameLossEvidence) {
    era_test_usb_sampler_reset(true);
    uint32_t age_ms = 0;
    ASSERT_TRUE(era_test_usb_sampler_observe(1000U, 7U, false, false, &age_ms));
    ASSERT_TRUE(era_test_usb_sampler_observe(2049000U, 7U, false, false, &age_ms));
    EXPECT_EQ(age_ms, 0U);
    EXPECT_FALSE(era_usb_session_frames_lost());
    ASSERT_TRUE(era_test_usb_sampler_observe(2249000U, 7U, false, false, &age_ms));
    ASSERT_TRUE(era_test_usb_sampler_observe(2349000U, 7U, false, false, &age_ms));
    EXPECT_TRUE(era_usb_session_frames_lost());
}

TEST(EraUsbSessionSampler, NeverConfiguredPortDoesNotBecomeSleepingHost) {
    era_test_usb_sampler_reset(false);
    uint32_t age_ms = 0;
    for (uint32_t now = 1000U; now < 1000000U; now += 1000U) {
        ASSERT_TRUE(era_test_usb_sampler_observe(now, 0U, false, false, &age_ms));
    }
    EXPECT_GE(age_ms, 300U);
    EXPECT_FALSE(era_usb_session_frames_lost());
}
