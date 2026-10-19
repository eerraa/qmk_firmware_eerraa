// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../../common/storage/era_eeprom_layout.h"

/* ERA-owned EEPROM storage sits before VIA/dynamic-keymap EEPROM ranges. */
#define VIA_EEPROM_MAGIC_ADDR ERA_EEPROM_CONFIG_END
#if !defined(VIA_ENABLE)
#    define DYNAMIC_KEYMAP_EEPROM_ADDR ERA_EEPROM_CONFIG_END
#endif

/* The tap-dance slot keycodes start at this board's first custom keycode. */
#define ERA_TAP_DANCE_KEYCODE_BASE QK_KB_0

/* VIA-persistent tap-hold controls. */
#define TAPPING_TERM_PER_KEY
#define PERMISSIVE_HOLD_PER_KEY
#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#define RETRO_TAPPING_PER_KEY

/* Riley's three WS2812s stay in the normal RGBLight effect range. The three
   lighting layers are only overlays for lock/Indicator-Only policy. Do not use
   RGBLIGHT_LAYERS_OVERRIDE_RGB_OFF: sleep/suspend/host-loss must win. */
#define RGBLIGHT_LAYERS
#define RGBLIGHT_MAX_LAYERS 3

/* Reset */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U
