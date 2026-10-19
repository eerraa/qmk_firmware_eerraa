// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
 
#pragma once

/* Reset */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
/* 1000U on every ERA board (owner decision 2026-08-11). Since 2026-08-11 this
   board takes the ERA common layer, so the window is the non-blocking one:
   the pre-copy hook arms it before crt0's copy loops and
   era_common_features_task() closes it from the keyboard loop, so the timeout
   is the window's guaranteed minimum and not a boot stall. */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U

/* --- ERA common layer ---------------------------------------------------- */

#include "../../common/storage/era_eeprom_layout.h"

/* ERA-owned EEPROM storage sits before VIA/dynamic-keymap EEPROM ranges. */
#define VIA_EEPROM_MAGIC_ADDR ERA_EEPROM_CONFIG_END
#if !defined(VIA_ENABLE)
#    define DYNAMIC_KEYMAP_EEPROM_ADDR ERA_EEPROM_CONFIG_END
#endif

/* The tap-dance slot keycodes start at this board's first custom keycode.
   A board fact, so it is stated here and not in make -- and stated in config.h
   rather than the board header, because a config.h is force-included into every
   translation unit and a board header is only visible to a unit that reaches
   QMK_KEYBOARD_H. */
#define ERA_TAP_DANCE_KEYCODE_BASE QK_KB_0

/* The two lock-indicator slots, and which LED each paints. Board geometry, so
   it is here rather than in make: LED 50 sits at matrix [3,0], the Caps Lock
   key, and LED 1 at matrix [0,15], the Scroll Lock key. Both are ordinary
   per-key LEDs of the 92-LED panel, which is the whole point of this board's
   indicator -- with the master switch off they go back to rendering the
   effect (owner decision 2026-08-18). */
#define ERA_RGB_INDICATOR_1_LED 50
#define ERA_RGB_INDICATOR_2_LED 1

/* VIA-persistent tap-hold controls */
#define TAPPING_TERM_PER_KEY
#define PERMISSIVE_HOLD_PER_KEY
#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#define RETRO_TAPPING_PER_KEY
