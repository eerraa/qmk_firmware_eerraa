// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "matrix.h"

enum {
    ERA_MATRIX_DEBOUNCE_PROFILE_BALANCED = 0,
    ERA_MATRIX_DEBOUNCE_PROFILE_FAST,
    ERA_MATRIX_DEBOUNCE_PROFILE_ADVANCED,
    ERA_MATRIX_DEBOUNCE_PROFILE_COUNT,
};

#define ERA_MATRIX_DEBOUNCE_MIN_DELAY_MS 1U
#define ERA_MATRIX_DEBOUNCE_MAX_DELAY_MS 30U

#ifndef ERA_MATRIX_DEBOUNCE_DEFAULT_DELAY_MS
#    ifdef DEBOUNCE
#        define ERA_MATRIX_DEBOUNCE_DEFAULT_DELAY_MS DEBOUNCE
#    else
#        define ERA_MATRIX_DEBOUNCE_DEFAULT_DELAY_MS 5U
#    endif
#endif

typedef struct {
    uint8_t mode;
    uint8_t pre_ms;
    uint8_t post_ms;
} era_matrix_debounce_config_t;

uint8_t era_matrix_debounce_clamp_delay(uint8_t delay);
void    era_matrix_debounce_default_config(era_matrix_debounce_config_t *config);
void    era_matrix_debounce_init(void);
void    era_matrix_debounce_apply_config(const era_matrix_debounce_config_t *config);
bool    era_matrix_debounce_update(matrix_row_t raw[], matrix_row_t cooked[], bool changed);
