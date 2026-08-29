// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"
#include "test_common.hpp"

extern "C" {
#include "rgb_matrix.h"
#include "wait.h"

static uint32_t g_driver_flush_count;

static void test_driver_init(void) {}
static void test_driver_set_color(int, uint8_t, uint8_t, uint8_t) {}
static void test_driver_set_color_all(uint8_t, uint8_t, uint8_t) {}
static void test_driver_flush(void) {
    g_driver_flush_count++;
}

led_config_t g_led_config = {};
const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = test_driver_init,
    .set_color     = test_driver_set_color,
    .set_color_all = test_driver_set_color_all,
    .flush         = test_driver_flush,
};
}

namespace {

constexpr uint32_t kQuietMs = ERA_STORAGE_QUIET_DEFER_MS;

bool     g_rearm_during_write;
uint32_t g_write_count;
bool     g_status_policy_active;

class EraRgbMatrixPersistence : public testing::Test {
   protected:
    void SetUp() override {
        g_rearm_during_write = false;
        eeconfig_flush_rgb_matrix_deferred_now();
        timer_clear();
        g_write_count = 0;
        g_driver_flush_count = 0;
        g_status_policy_active = false;
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

extern "C" void rgb_matrix_render_policy_kb(rgb_matrix_render_policy_t *policy) {
    if (g_status_policy_active) {
        policy->flags |= RGB_MATRIX_RENDER_POLICY_STATUS_ACTIVE |
                         RGB_MATRIX_RENDER_POLICY_STATUS_DIRTY |
                         RGB_MATRIX_RENDER_POLICY_ALLOW_DISABLED;
    }
}

extern "C" bool rgb_matrix_render_status_kb(const rgb_matrix_render_policy_t *) {
    return g_status_policy_active;
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

TEST_F(EraRgbMatrixPersistence, RenderPolicyRefreshDoesNotWaitForNextFrameEpoch) {
    /* Put the state machine into its idle SYNCING arm at time zero. With no
       explicit refresh, RGB_MATRIX_LED_FLUSH_LIMIT would keep it there until
       16 ms. */
    rgb_matrix_config.enable = 0;
    rgb_matrix_enable_noeeprom();
    rgb_matrix_config.mode = RGB_MATRIX_NONE;
    rgb_matrix_task(); // STARTING -> RENDERING
    rgb_matrix_task(); // RENDERING -> SYNCING for NONE
    ASSERT_EQ(g_driver_flush_count, 0U);

    g_status_policy_active = true;
    rgb_matrix_render_policy_request_refresh();
    rgb_matrix_task(); // STARTING -> RENDERING
    rgb_matrix_task(); // RENDERING -> FLUSHING (STATUS)
    rgb_matrix_task(); // FLUSHING -> hardware push

    EXPECT_EQ(g_driver_flush_count, 1U);
}
