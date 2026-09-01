// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

enum riley_indicator_mode {
    RILEY_INDICATOR_RGB_EFFECT = 0,
    RILEY_INDICATOR_CAPS_LOCK,
    RILEY_INDICATOR_SCROLL_LOCK,
    RILEY_INDICATOR_NUM_LOCK,
};

enum riley_indicator_slot {
    RILEY_INDICATOR_SLOT_1 = 0,
    RILEY_INDICATOR_SLOT_2,
    RILEY_INDICATOR_SLOT_3,
    RILEY_INDICATOR_SLOT_COUNT,
};

typedef struct __attribute__((packed)) {
    uint8_t flags;
    HSV     indicator_hsv[RILEY_INDICATOR_SLOT_COUNT];
} riley_config_t;

extern riley_config_t g_riley_config;

#ifdef VIA_ENABLE
enum riley_custom_value_id {
    id_custom_riley_ind1_mode = 13,
    id_custom_riley_ind1_brightness,
    id_custom_riley_ind1_color,
    id_custom_riley_ind2_mode,
    id_custom_riley_ind2_brightness,
    id_custom_riley_ind2_color,
    id_custom_riley_ind3_mode,
    id_custom_riley_ind3_brightness,
    id_custom_riley_ind3_color,
    id_custom_riley_indicator_only,
    id_custom_riley_velocikey_enable,
};
#endif

#ifdef RILEY_RGB_INDICATOR_TEST
uint8_t riley_indicator_mode_for_testing(uint8_t slot);
bool    riley_indicator_only_for_testing(void);
#endif
