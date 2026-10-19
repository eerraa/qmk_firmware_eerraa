// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include "gtest/gtest.h"

extern "C" {
#include "keyboards/era/common/system/era_matrix_debounce_runtime.h"
#include "timer.h"
void set_time(uint32_t time);
void advance_time(uint32_t ms);
void reset_access_counter(void);
uint32_t current_access_counter(void);
}

namespace {
struct Matrix {
    std::array<matrix_row_t, MATRIX_ROWS_PER_HAND> raw{};
    std::array<matrix_row_t, MATRIX_ROWS_PER_HAND> cooked{};

    Matrix() {
        timer_init();
        era_matrix_debounce_init();
    }
    void configure(uint8_t mode, uint8_t press = 7U, uint8_t release = 11U) {
        const era_matrix_debounce_config_t config{mode, press, release};
        era_matrix_debounce_apply_config(&config);
    }
    bool scan(bool raw_changed = false) {
        return era_matrix_debounce_update(raw.data(), cooked.data(), raw_changed);
    }
    void settle(uint32_t duration = 40U) {
        for (uint32_t i = 0; i < duration; ++i) {
            advance_time(1U);
            scan();
        }
    }
};
}

TEST(EraMatrixDebounceRuntime, PendingPressSurvivesReconfigurationWithoutAnotherRawEdge) {
    for (uint8_t next = 0; next < ERA_MATRIX_DEBOUNCE_PROFILE_COUNT; ++next) {
        SCOPED_TRACE(next);
        Matrix matrix;
        matrix.configure(ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED, 5U, 5U);
        matrix.raw[0] = 1U;
        ASSERT_FALSE(matrix.scan(true));
        ASSERT_EQ(matrix.cooked[0], 0U);
        advance_time(1U);
        matrix.configure(next);
        matrix.scan();
        matrix.settle();
        EXPECT_EQ(matrix.cooked[0], 1U);
    }
}

TEST(EraMatrixDebounceRuntime, PendingReleaseSurvivesAllNineProfileTransitions) {
    for (uint8_t previous = 0; previous < ERA_MATRIX_DEBOUNCE_PROFILE_COUNT; ++previous) {
        for (uint8_t next = 0; next < ERA_MATRIX_DEBOUNCE_PROFILE_COUNT; ++next) {
            SCOPED_TRACE(previous);
            SCOPED_TRACE(next);
            Matrix matrix;
            matrix.configure(previous, 5U, 5U);
            matrix.raw[0] = 1U;
            matrix.scan(true);
            if (previous == ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED) {
                matrix.settle(5U);
            }
            ASSERT_EQ(matrix.cooked[0], 1U);
            advance_time(1U);
            matrix.raw[0] = 0U;
            ASSERT_FALSE(matrix.scan(true));
            ASSERT_EQ(matrix.cooked[0], 1U);
            matrix.configure(next);
            matrix.scan();
            matrix.settle();
            EXPECT_EQ(matrix.cooked[0], 0U);
        }
    }
}

TEST(EraMatrixDebounceRuntime, StableRowsHaveNoSyntheticEdgeAndReturnToClockFreeIdle) {
    for (uint8_t mode = 0; mode < ERA_MATRIX_DEBOUNCE_PROFILE_COUNT; ++mode) {
        for (matrix_row_t state : {matrix_row_t{0}, matrix_row_t{1}}) {
            Matrix matrix;
            matrix.raw.fill(state);
            matrix.cooked.fill(state);
            matrix.configure(mode);
            EXPECT_FALSE(matrix.scan());
            reset_access_counter();
            EXPECT_FALSE(matrix.scan());
            EXPECT_EQ(current_access_counter(), 0U);
            EXPECT_EQ(matrix.raw, matrix.cooked);
        }
    }
}

TEST(EraMatrixDebounceRuntime, PendingPressReearnsNewWindowAcrossTimerWrap) {
    Matrix matrix;
    set_time(UINT32_MAX - 3U);
    matrix.configure(ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED, 5U, 5U);
    matrix.raw[0] = 1U;
    ASSERT_FALSE(matrix.scan(true));
    advance_time(2U);
    matrix.configure(ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED, 7U, 7U);
    EXPECT_FALSE(matrix.scan());
    matrix.settle(6U);
    EXPECT_EQ(matrix.cooked[0], 0U);
    advance_time(1U);
    EXPECT_TRUE(matrix.scan());
    EXPECT_EQ(matrix.cooked[0], 1U);
}

TEST(EraMatrixDebounceRuntime, RepeatedConfigurationBeforeScanKeepsOneReconciliationDue) {
    Matrix matrix;
    matrix.raw[0] = 1U;
    ASSERT_FALSE(matrix.scan(true));
    matrix.configure(ERA_MATRIX_DEBOUNCE_PROFILE_FAST);
    matrix.configure(ERA_MATRIX_DEBOUNCE_PROFILE_ADVANCED);
    matrix.configure(ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED);
    matrix.scan();
    matrix.settle();
    EXPECT_EQ(matrix.cooked[0], 1U);
    EXPECT_FALSE(matrix.scan());
}

TEST(EraMatrixDebounceRuntime, EveryRowAndColumnIsReconciledUnderTheNewProfile) {
    constexpr matrix_row_t mask = (matrix_row_t{1} << MATRIX_COLS) - 1U;
    for (uint8_t mode = 0; mode < ERA_MATRIX_DEBOUNCE_PROFILE_COUNT; ++mode) {
        Matrix matrix;
        for (size_t row = 0; row < matrix.raw.size(); ++row) {
            matrix.raw[row] = static_cast<matrix_row_t>(0x155U ^ row) & mask;
            matrix.cooked[row] = (~matrix.raw[row]) & mask;
        }
        matrix.configure(mode);
        matrix.scan();
        matrix.settle();
        EXPECT_EQ(matrix.raw, matrix.cooked);
    }
}
