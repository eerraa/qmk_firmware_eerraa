// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rgblight.h"

/* The underglow (RGBLight) Pulse effects: ERA's keypress-reactive modes on
 * QMK's own channel-2 effect list.
 *
 * Four modes, appended after Twinkle in `quantum/rgblight/rgblight_modes.h`
 * under `ERA_RGBLIGHT_PULSE_ENABLE`, so the ordinary VIA Effect dropdown
 * selects them and the ordinary Effect Speed slider -- `rgblight_config.speed`,
 * 0..255, which no stock RGBLight effect reads -- sets the pulse width: the
 * family width of `era_pulse_policy.h`, 5 + speed milliseconds, 5 ms at 0 and
 * 260 ms at 255, the same control the PWM backlight's Pulse Speed is. The
 * boards' `keyboard.json` default speed 15 is the 20 ms default.
 *
 * The state machine is the family one (`era_pulse_policy.h`): what the PWM
 * backlight's Pulse does, this does, on the whole effect range. The engine
 * mirrors `era_backlight.c`: the switch edge writes bounded state and arms a
 * ChibiOS one-shot; the expiry callback sets one flag; rendering happens only
 * from the RGBLight animation tick, which these modes run at 1 ms, and only
 * when the requested output or the configured colour changed.
 *
 * Mode entry, re-entry and disable reset synchronously at the RGBLight core
 * boundary, before another physical press can arrive. Actual RGBLight sleep
 * and wake own suspend state; the USB hooks do not bypass RGB Sleep master.
 */

/* One of the four Pulse modes? They are single modes, so `base_mode == mode`. */
bool era_rgblight_pulse_mode(uint8_t mode);

/* Milliseconds the current Effect Speed gives one pulse. */
uint16_t era_rgblight_pulse_duration_ms(void);

void era_rgblight_pulse_init(void);
void era_rgblight_pulse_reset(void);
void era_rgblight_pulse_refresh(void);

/* Physical matrix-key edge, before tap/hold resolution and semantic filters
   (`era_common_features_switch_event()`). Ignored unless RGBLight is enabled
   and a Pulse mode is selected. */
void era_rgblight_pulse_note_key_event(uint8_t row, uint8_t col, bool pressed);

/* The RGBLight animation effect for the four modes: `rgblight_timer_task()`
   in `quantum/rgblight/rgblight.c` calls it once a millisecond while one of
   them is selected. */
void era_rgblight_pulse_effect(animation_status_t *anim);

/* Called by actual RGBLight suspend/wake, after the RGB Sleep master gate. */
void era_rgblight_pulse_suspend(void);
void era_rgblight_pulse_resume(void);
