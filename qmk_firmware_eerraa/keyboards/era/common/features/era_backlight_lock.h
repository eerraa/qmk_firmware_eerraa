// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

/* Keep QMK's backlight subsystem logically enabled when the PWM rail is an
 * indicator supply rather than an independently switchable lamp.
 *
 * The policy is about QMK's persisted `enable` bit, not about PWM duty. A board
 * may still use Breathing, Pulse effects, USB sleep, or explicit brightness
 * changes that momentarily drive the hardware to zero. What it may not persist
 * is a disabled subsystem that leaves fixed lock indicators permanently dark.
 *
 * This policy therefore composes with `ERA_BACKLIGHT_EFFECT_ENABLE`. The effect
 * engine owns transient output; this unit owns only the enable boundary and the
 * backlight keycodes that could cross it.
 */

void era_backlight_lock_init(void);
bool era_backlight_lock_process_record(uint16_t keycode, keyrecord_t *record);
