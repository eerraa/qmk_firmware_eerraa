// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* The odessey family's shared types, extracted from the two board headers on
   2026-08-13 with the board content itself. `odessey60h.h` and `odessey60s.h`
   were string-identical after the board-token substitution; what stays in each
   board header is the tap-dance keycode enum, which a source keymap names and
   which is per board by convention even where the values agree. */

enum odessey_indicator_mode {
    ODESSEY_INDICATOR_OFF = 0,
    ODESSEY_INDICATOR_CAPS_LOCK,
    ODESSEY_INDICATOR_SCROLL_LOCK,
    ODESSEY_INDICATOR_NUM_LOCK
};

typedef union {
    uint32_t raw;
    struct {
        uint8_t reserved0:4;
        uint8_t indicator_mode:2;
        uint8_t indicator_mode_initialized:1;
        uint8_t reserved1:1;
        HSV     indicator_hsv;
    } __attribute__((packed));
} odessey_config_t;

extern odessey_config_t g_odessey_config;

#ifdef VIA_ENABLE
enum odessey_custom_value_id {
    id_custom_indicator_select = 1,
    id_custom_indicator_brightness,
    id_custom_indicator_color,
    id_custom_velocikey_enable
};
#endif
