// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_matrix_debounce_runtime.h"

#include <string.h>

#include "timer.h"
#include "util.h"

#define ERA_MATRIX_DEBOUNCE_ELAPSED 0U

#ifndef ERA_MATRIX_DEBOUNCE_MATRIX_ROWS
#    define ERA_MATRIX_DEBOUNCE_MATRIX_ROWS MATRIX_ROWS_PER_HAND
#endif

typedef struct {
    bool    pressed : 1;
    uint8_t time : 7;
} era_matrix_debounce_asym_counter_t;

static era_matrix_debounce_config_t       debounce_config;
static uint8_t                            debounce_counters[ERA_MATRIX_DEBOUNCE_MATRIX_ROWS * MATRIX_COLS];
static era_matrix_debounce_asym_counter_t asym_debounce_counters[ERA_MATRIX_DEBOUNCE_MATRIX_ROWS * MATRIX_COLS];
static fast_timer_t                       debounce_last_time;
static bool                               debounce_config_loaded;
static bool                               counters_need_update;
static bool                               matrix_need_update;
static bool                               cooked_changed;

static inline fast_timer_t __attribute__((always_inline)) era_matrix_debounce_time_read(void) {
    return timer_read_fast();
}

uint8_t era_matrix_debounce_clamp_delay(uint8_t delay) {
    if (delay < ERA_MATRIX_DEBOUNCE_MIN_DELAY_MS) {
        return ERA_MATRIX_DEBOUNCE_MIN_DELAY_MS;
    }
    if (delay > ERA_MATRIX_DEBOUNCE_MAX_DELAY_MS) {
        return ERA_MATRIX_DEBOUNCE_MAX_DELAY_MS;
    }
    return delay;
}

void era_matrix_debounce_default_config(era_matrix_debounce_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->mode    = ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED;
    config->pre_ms  = era_matrix_debounce_clamp_delay((uint8_t)ERA_MATRIX_DEBOUNCE_DEFAULT_DELAY_MS);
    config->post_ms = config->pre_ms;
}

static void era_matrix_debounce_normalize_config(era_matrix_debounce_config_t *config) {
    if (config == NULL) {
        return;
    }

    if (config->mode >= ERA_MATRIX_DEBOUNCE_PROFILE_COUNT) {
        config->mode = ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED;
    }

    config->pre_ms  = era_matrix_debounce_clamp_delay(config->pre_ms);
    config->post_ms = era_matrix_debounce_clamp_delay(config->post_ms);
    if (config->mode == ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED) {
        config->post_ms = config->pre_ms;
    }
}

static void era_matrix_debounce_reset_runtime(void) {
    memset(debounce_counters, ERA_MATRIX_DEBOUNCE_ELAPSED, sizeof(debounce_counters));
    memset(asym_debounce_counters, ERA_MATRIX_DEBOUNCE_ELAPSED, sizeof(asym_debounce_counters));
    debounce_last_time   = era_matrix_debounce_time_read();
    counters_need_update = false;
    matrix_need_update   = false;
    cooked_changed       = false;
}

void era_matrix_debounce_init(void) {
    era_matrix_debounce_default_config(&debounce_config);
    debounce_config_loaded = true;
    era_matrix_debounce_reset_runtime();
}

void era_matrix_debounce_apply_config(const era_matrix_debounce_config_t *config) {
    if (config != NULL) {
        debounce_config = *config;
    } else {
        era_matrix_debounce_default_config(&debounce_config);
    }
    era_matrix_debounce_normalize_config(&debounce_config);
    debounce_config_loaded = true;
    era_matrix_debounce_reset_runtime();
}

static inline void __attribute__((always_inline)) era_matrix_debounce_ensure_config(void) {
    if (!debounce_config_loaded) {
        era_matrix_debounce_init();
    }
}

static inline uint8_t __attribute__((always_inline)) era_matrix_debounce_current_balanced_delay(void) {
    return debounce_config.pre_ms;
}

static inline uint8_t __attribute__((always_inline)) era_matrix_debounce_current_fast_delay(void) {
    return debounce_config.post_ms;
}

static inline uint8_t __attribute__((always_inline)) era_matrix_debounce_current_press_delay(void) {
    return debounce_config.pre_ms;
}

static inline uint8_t __attribute__((always_inline)) era_matrix_debounce_current_release_delay(void) {
    return debounce_config.post_ms;
}

static bool __attribute__((noinline)) era_matrix_debounce_run_sym_defer(matrix_row_t raw[], matrix_row_t cooked[], bool changed);
static bool __attribute__((noinline)) era_matrix_debounce_run_sym_eager(matrix_row_t raw[], matrix_row_t cooked[], bool changed);
static bool __attribute__((noinline)) era_matrix_debounce_run_asym(matrix_row_t raw[], matrix_row_t cooked[], bool changed);

bool era_matrix_debounce_update(matrix_row_t raw[], matrix_row_t cooked[], bool changed) {
    era_matrix_debounce_ensure_config();

    if (!changed && !counters_need_update && !matrix_need_update) {
        return false;
    }

    switch (debounce_config.mode) {
        case ERA_MATRIX_DEBOUNCE_PROFILE_FAST:
            return era_matrix_debounce_run_sym_eager(raw, cooked, changed);
        case ERA_MATRIX_DEBOUNCE_PROFILE_ADVANCED:
            return era_matrix_debounce_run_asym(raw, cooked, changed);
        case ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED:
        default:
            return era_matrix_debounce_run_sym_defer(raw, cooked, changed);
    }
}

static void era_matrix_debounce_update_sym_defer_counters(matrix_row_t raw[], matrix_row_t cooked[], uint8_t elapsed_time) {
    counters_need_update = false;

    for (uint8_t row = 0; row < ERA_MATRIX_DEBOUNCE_MATRIX_ROWS; row++) {
        uint16_t row_offset = row * MATRIX_COLS;

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t index = row_offset + col;

            if (debounce_counters[index] != ERA_MATRIX_DEBOUNCE_ELAPSED) {
                if (debounce_counters[index] <= elapsed_time) {
                    debounce_counters[index] = ERA_MATRIX_DEBOUNCE_ELAPSED;
                    matrix_row_t col_mask    = (MATRIX_ROW_SHIFTER << col);
                    matrix_row_t cooked_next = (cooked[row] & ~col_mask) | (raw[row] & col_mask);
                    if (cooked_next != cooked[row]) {
                        cooked_changed = true;
                    }
                    cooked[row] = cooked_next;
                } else {
                    debounce_counters[index] -= elapsed_time;
                    counters_need_update = true;
                }
            }
        }
    }
}

static void era_matrix_debounce_start_sym_defer_counters(matrix_row_t raw[], matrix_row_t cooked[]) {
    uint8_t debounce_delay = era_matrix_debounce_current_balanced_delay();

    for (uint8_t row = 0; row < ERA_MATRIX_DEBOUNCE_MATRIX_ROWS; row++) {
        uint16_t     row_offset = row * MATRIX_COLS;
        matrix_row_t delta      = raw[row] ^ cooked[row];

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t index = row_offset + col;

            if (delta & (MATRIX_ROW_SHIFTER << col)) {
                if (debounce_counters[index] == ERA_MATRIX_DEBOUNCE_ELAPSED) {
                    debounce_counters[index] = debounce_delay;
                    counters_need_update     = true;
                }
            } else {
                debounce_counters[index] = ERA_MATRIX_DEBOUNCE_ELAPSED;
            }
        }
    }
}

static bool __attribute__((noinline)) era_matrix_debounce_run_sym_defer(matrix_row_t raw[], matrix_row_t cooked[], bool changed) {
    bool updated_last = false;
    cooked_changed    = false;

    if (counters_need_update) {
        fast_timer_t now          = era_matrix_debounce_time_read();
        fast_timer_t elapsed_time = TIMER_DIFF_FAST(now, debounce_last_time);

        debounce_last_time = now;
        updated_last       = true;

        if (elapsed_time > 0) {
            era_matrix_debounce_update_sym_defer_counters(raw, cooked, MIN(elapsed_time, UINT8_MAX));
        }
    }

    if (changed) {
        if (!updated_last) {
            debounce_last_time = era_matrix_debounce_time_read();
        }

        era_matrix_debounce_start_sym_defer_counters(raw, cooked);
    }

    return cooked_changed;
}

static void era_matrix_debounce_update_sym_eager_counters(uint8_t elapsed_time) {
    counters_need_update = false;
    matrix_need_update   = false;

    for (uint8_t row = 0; row < ERA_MATRIX_DEBOUNCE_MATRIX_ROWS; row++) {
        uint16_t row_offset = row * MATRIX_COLS;

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t index = row_offset + col;

            if (debounce_counters[index] != ERA_MATRIX_DEBOUNCE_ELAPSED) {
                if (debounce_counters[index] <= elapsed_time) {
                    debounce_counters[index] = ERA_MATRIX_DEBOUNCE_ELAPSED;
                    matrix_need_update       = true;
                } else {
                    debounce_counters[index] -= elapsed_time;
                    counters_need_update = true;
                }
            }
        }
    }
}

static void era_matrix_debounce_transfer_sym_eager_values(matrix_row_t raw[], matrix_row_t cooked[]) {
    uint8_t debounce_delay = era_matrix_debounce_current_fast_delay();
    matrix_need_update     = false;

    for (uint8_t row = 0; row < ERA_MATRIX_DEBOUNCE_MATRIX_ROWS; row++) {
        uint16_t     row_offset   = row * MATRIX_COLS;
        matrix_row_t delta        = raw[row] ^ cooked[row];
        matrix_row_t existing_row = cooked[row];

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t     index    = row_offset + col;
            matrix_row_t col_mask = (MATRIX_ROW_SHIFTER << col);

            if ((delta & col_mask) && debounce_counters[index] == ERA_MATRIX_DEBOUNCE_ELAPSED) {
                debounce_counters[index] = debounce_delay;
                counters_need_update     = true;
                existing_row ^= col_mask;
                cooked_changed = true;
            }
        }
        cooked[row] = existing_row;
    }
}

static bool __attribute__((noinline)) era_matrix_debounce_run_sym_eager(matrix_row_t raw[], matrix_row_t cooked[], bool changed) {
    bool updated_last = false;
    cooked_changed    = false;

    if (counters_need_update) {
        fast_timer_t now          = era_matrix_debounce_time_read();
        fast_timer_t elapsed_time = TIMER_DIFF_FAST(now, debounce_last_time);

        debounce_last_time = now;
        updated_last       = true;

        if (elapsed_time > 0) {
            era_matrix_debounce_update_sym_eager_counters(MIN(elapsed_time, UINT8_MAX));
        }
    }

    if (changed || matrix_need_update) {
        if (!updated_last) {
            debounce_last_time = era_matrix_debounce_time_read();
        }

        era_matrix_debounce_transfer_sym_eager_values(raw, cooked);
    }

    return cooked_changed;
}

static void era_matrix_debounce_update_asym_counters(matrix_row_t raw[], matrix_row_t cooked[], uint8_t elapsed_time) {
    counters_need_update = false;
    matrix_need_update   = false;

    for (uint8_t row = 0; row < ERA_MATRIX_DEBOUNCE_MATRIX_ROWS; row++) {
        uint16_t row_offset = row * MATRIX_COLS;

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t index = row_offset + col;

            if (asym_debounce_counters[index].time != ERA_MATRIX_DEBOUNCE_ELAPSED) {
                if (asym_debounce_counters[index].time <= elapsed_time) {
                    asym_debounce_counters[index].time = ERA_MATRIX_DEBOUNCE_ELAPSED;

                    if (asym_debounce_counters[index].pressed) {
                        matrix_need_update = true;
                    } else {
                        matrix_row_t col_mask    = (MATRIX_ROW_SHIFTER << col);
                        matrix_row_t cooked_next = (cooked[row] & ~col_mask) | (raw[row] & col_mask);
                        if (cooked_next != cooked[row]) {
                            cooked_changed = true;
                        }
                        cooked[row] = cooked_next;
                    }
                } else {
                    asym_debounce_counters[index].time -= elapsed_time;
                    counters_need_update = true;
                }
            }
        }
    }
}

static void era_matrix_debounce_transfer_asym_values(matrix_row_t raw[], matrix_row_t cooked[]) {
    uint8_t press_delay   = era_matrix_debounce_current_press_delay();
    uint8_t release_delay = era_matrix_debounce_current_release_delay();
    matrix_need_update    = false;

    for (uint8_t row = 0; row < ERA_MATRIX_DEBOUNCE_MATRIX_ROWS; row++) {
        uint16_t     row_offset = row * MATRIX_COLS;
        matrix_row_t delta      = raw[row] ^ cooked[row];

        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            uint16_t     index    = row_offset + col;
            matrix_row_t col_mask = (MATRIX_ROW_SHIFTER << col);

            if (delta & col_mask) {
                if (asym_debounce_counters[index].time == ERA_MATRIX_DEBOUNCE_ELAPSED) {
                    asym_debounce_counters[index].pressed = (raw[row] & col_mask) != 0;
                    asym_debounce_counters[index].time    = asym_debounce_counters[index].pressed ? press_delay : release_delay;
                    counters_need_update                  = true;

                    if (asym_debounce_counters[index].pressed) {
                        cooked[row] ^= col_mask;
                        cooked_changed = true;
                    }
                }
            } else if (asym_debounce_counters[index].time != ERA_MATRIX_DEBOUNCE_ELAPSED && !asym_debounce_counters[index].pressed) {
                asym_debounce_counters[index].time = ERA_MATRIX_DEBOUNCE_ELAPSED;
            }
        }
    }
}

static bool __attribute__((noinline)) era_matrix_debounce_run_asym(matrix_row_t raw[], matrix_row_t cooked[], bool changed) {
    bool updated_last = false;
    cooked_changed    = false;

    if (counters_need_update) {
        fast_timer_t now          = era_matrix_debounce_time_read();
        fast_timer_t elapsed_time = TIMER_DIFF_FAST(now, debounce_last_time);

        debounce_last_time = now;
        updated_last       = true;

        if (elapsed_time > 0) {
            era_matrix_debounce_update_asym_counters(raw, cooked, MIN(elapsed_time, UINT8_MAX));
        }
    }

    if (changed || matrix_need_update) {
        if (!updated_last) {
            debounce_last_time = era_matrix_debounce_time_read();
        }

        era_matrix_debounce_transfer_asym_values(raw, cooked);
    }

    return cooked_changed;
}
