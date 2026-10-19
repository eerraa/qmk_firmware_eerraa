// Copyright 2023 eerraa
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* BACKLIGHT PWM */
#define BACKLIGHT_PWM_DRIVER PWMD7
#define BACKLIGHT_PWM_CHANNEL RP2040_PWM_CHANNEL_B

/* Reset. This board had no double-tap window at all until 2026-08-11: with no
   RP2040_BOOTLOADER_DOUBLE_TAP_RESET the magic word does not exist, so the
   copy-to-RAM image's two linker ASSERTs on it could not pass. 1000U is the
   value on every ERA board. */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
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

/* VIA-persistent tap-hold controls */
#define TAPPING_TERM_PER_KEY
#define PERMISSIVE_HOLD_PER_KEY
#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#define RETRO_TAPPING_PER_KEY
