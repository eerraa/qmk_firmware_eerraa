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

enum {
    ERA_SOCD_MODE_LAST_INPUT_WINS = 1,
};

void era_socd_init(void);
void era_socd_reload_from_eeprom(void);
bool era_socd_process_record(uint16_t keycode, keyrecord_t *record);
bool era_socd_is_bound_keycode(uint16_t keycode);
void era_socd_save_pair(uint8_t pair);
bool era_socd_set_enabled(uint8_t pair, bool enabled);
bool era_socd_get_enabled(uint8_t pair);
bool era_socd_set_keycode(uint8_t pair, uint8_t key_index, uint16_t keycode);
uint16_t era_socd_get_keycode(uint8_t pair, uint8_t key_index);
bool era_socd_set_mode(uint8_t pair, uint8_t mode);
uint8_t era_socd_get_mode(uint8_t pair);
