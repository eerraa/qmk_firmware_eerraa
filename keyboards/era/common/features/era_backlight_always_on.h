// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* The PWM backlight as an indicator supply rather than as lighting.
 *
 * Three ERA boards wire the backlight pin to nothing but their lock LEDs --
 * `divine` (three), `sirind/klein_hs` and `sirind/klein_sd` (two each) -- so
 * the rail has no off state that is a preference rather than a broken
 * keyboard, and those three ship no lighting menu and no lighting keycode at
 * all (owner decision 2026-08-18). This is the other half of that decision:
 * with no surface, a stored `enable = 0` from an earlier firmware or an
 * earlier keymap would strand the indicators dark with EEPROM CLEAN as the
 * only way back.
 *
 * It is a separate unit from `era_backlight.[ch]` and the two selectors are
 * refused together, because they are contradictory claims about the same pin:
 * that file is the rail as a backlight, with keypress blinks that deliberately
 * drive it to zero.
 */

void era_backlight_always_on_init(void);
