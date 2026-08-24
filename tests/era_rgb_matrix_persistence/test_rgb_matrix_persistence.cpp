// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"
#include "test_common.hpp"

extern "C" {
#include "rgb_matrix.h"
#include "wait.h"

led_config_t              g_led_config      = {};
const rgb_matrix_driver_t rgb_matrix_driver = {};
}

namespace {

constexpr uint32_t kQuietMs = ERA_STORAGE_QUIET_DEFER_MS;

bool     g_rearm_during_write;
uint32_t g_write_count;

class EraRgbMatrixPersistence : public testing::Test {
   protected:
    void SetUp() override {
        g_rearm_during_write = false;
        eeconfig_flush_rgb_matrix_deferred_now();
        timer_clear();
        g_write_count = 0;
    }

    void TearDown() override {
        g_rearm_during_write = false;
        eeconfig_flush_rgb_matrix_deferred_now();
    }
};

} // namespace

extern "C" void __wrap_eeconfig_update_rgb_matrix(const rgb_config_t *config) {
    (void)config;
    g_write_count++;
    if (g_rearm_during_write) {
        g_rearm_during_write = false;
        /* The same live-config mutation and save arm reached by RM_TOGG. */
        rgb_matrix_toggle();
    }
}

TEST_F(EraRgbMatrixPersistence, NormalQuietFlushWritesOnceThenStaysClean) {
    rgb_matrix_toggle();
    wait_ms(kQuietMs + 1);

    eeconfig_flush_rgb_matrix_deferred_task();
    EXPECT_EQ(g_write_count, 1U);

    wait_ms(kQuietMs + 1);
    eeconfig_flush_rgb_matrix_deferred_task();
    EXPECT_EQ(g_write_count, 1U);
}

TEST_F(EraRgbMatrixPersistence, RearmInsideWriteSurvivesUntilNextQuietFlush) {
    rgb_matrix_toggle();
    wait_ms(kQuietMs + 1);
    g_rearm_during_write = true;

    eeconfig_flush_rgb_matrix_deferred_task();
    EXPECT_FALSE(g_rearm_during_write);
    EXPECT_EQ(g_write_count, 1U);

    eeconfig_flush_rgb_matrix_deferred_task();
    EXPECT_EQ(g_write_count, 1U);

    wait_ms(kQuietMs + 1);
    eeconfig_flush_rgb_matrix_deferred_task();
    EXPECT_EQ(g_write_count, 2U);

    wait_ms(kQuietMs + 1);
    eeconfig_flush_rgb_matrix_deferred_task();
    EXPECT_EQ(g_write_count, 2U);
}

TEST_F(EraRgbMatrixPersistence, DeferredNowWritesOnlyWhenPending) {
    eeconfig_flush_rgb_matrix_deferred_now();
    EXPECT_EQ(g_write_count, 0U);

    rgb_matrix_toggle();
    eeconfig_flush_rgb_matrix_deferred_now();
    EXPECT_EQ(g_write_count, 1U);

    eeconfig_flush_rgb_matrix_deferred_now();
    EXPECT_EQ(g_write_count, 1U);
}
