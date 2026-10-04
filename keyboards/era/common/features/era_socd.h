// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

enum {
    ERA_SOCD_PAIR_LR = 0,
    ERA_SOCD_PAIR_UD,
    ERA_SOCD_PAIR_COUNT,
    ERA_SOCD_PAIR_KEY_COUNT = 2,
};

/* The stored mode byte. H7S's kill switch uses the same values. */
enum {
    ERA_SOCD_MODE_LAST_INPUT = 1,
    ERA_SOCD_MODE_NEUTRAL,
    ERA_SOCD_MODE_FIRST_INPUT,
    ERA_SOCD_MODE_KEY_0_PRIORITY,
    ERA_SOCD_MODE_KEY_1_PRIORITY,
    ERA_SOCD_MODE_MAX = ERA_SOCD_MODE_KEY_1_PRIORITY,
};

void     era_socd_init(void);
void     era_socd_reload_from_eeprom(void);
bool     era_socd_is_bound_keycode(uint16_t keycode);
bool     era_socd_save_pair(uint8_t pair);
bool     era_socd_set_enabled(uint8_t pair, bool enabled);
bool     era_socd_get_enabled(uint8_t pair);
bool     era_socd_set_keycode(uint8_t pair, uint8_t key_index, uint16_t keycode);
uint16_t era_socd_get_keycode(uint8_t pair, uint8_t key_index);
bool     era_socd_set_mode(uint8_t pair, uint8_t mode);
uint8_t  era_socd_get_mode(uint8_t pair);
