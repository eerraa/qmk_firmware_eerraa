// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include "../../common/storage/era_eeprom_layout.h"

typedef struct __attribute__((packed)) {
    uint8_t lock_indicator_mode;
    uint8_t lock_indicator_overrides_rgb;
    uint8_t lock_indicator_hue;
    uint8_t lock_indicator_sat;
    uint8_t lock_indicator_val;
    uint8_t full_rgb_matrix_enabled;
    uint8_t reserved[2];
} tomak_era_keyboard_config_t;

_Static_assert(sizeof(tomak_era_keyboard_config_t) == ERA_EEPROM_KEYBOARD_CONFIG_SIZE, "TOMAK ERA keyboard config size changed.");
