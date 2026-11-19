// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* Reset */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
/* 1000U on every ERA board (owner decision 2026-08-11). This board takes the
   ERA common layer, so the window is the non-blocking one: the pre-copy hook
   arms it before crt0's copy loops and era_common_features_task() closes it
   from the keyboard loop, so the timeout is the window's guaranteed minimum
   and not a boot stall. */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U

/* --- ERA common layer ---------------------------------------------------- */

/* The tap-dance slot keycodes start at this board's first custom keycode.
   A board fact, so it is stated here and not in make -- and stated in config.h
   rather than the board header, because a config.h is force-included into every
   translation unit and a board header is only visible to a unit that reaches
   QMK_KEYBOARD_H. */
#define ERA_TAP_DANCE_KEYCODE_BASE QK_KB_0

/* The two lock-indicator slots, and which LED each paints. Board geometry, so
   it is here rather than in make: LEDs 0 and 1 are the first two elements of
   the ws2812 chain (RGB1, then RGB69), both under the Caps Lock key and both
   declared `LED_FLAG_INDICATOR` in `keyboard.json`. The other 67 are the
   per-key LEDs, in the same order and places as on CLINCH65H. */
#define ERA_RGB_INDICATOR_1_LED 0
#define ERA_RGB_INDICATOR_2_LED 1

/* VIA-persistent tap-hold controls */
#define TAPPING_TERM_PER_KEY
#define PERMISSIVE_HOLD_PER_KEY
#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#define RETRO_TAPPING_PER_KEY
