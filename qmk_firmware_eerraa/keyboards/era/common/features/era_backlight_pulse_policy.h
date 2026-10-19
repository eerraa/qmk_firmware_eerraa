// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_backlight.h"
#include "era_pulse_policy.h"

/* The PWM backlight's binding of the family Pulse policy
 * (`era_pulse_policy.h`): it maps the six backlight effects onto the policy's
 * two bits and the policy's on/off answer onto a level. The transitions
 * themselves are the shared header's, which `era_rgblight_pulse.c` runs too;
 * the production engine supplies the ChibiOS one-shot and the PWM writes, and
 * the host tests drive these wrappers directly. */
typedef era_pulse_state_t era_backlight_pulse_state_t;

static inline bool era_backlight_pulse_effect(uint8_t effect) {
    return effect >= ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS && effect <= ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD;
}

static inline bool era_backlight_pulse_hold_effect(uint8_t effect) {
    return effect == ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD || effect == ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD;
}

static inline bool era_backlight_pulse_default_on(uint8_t effect) {
    return effect == ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS || effect == ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD;
}

static inline void era_backlight_pulse_reset_runtime(era_backlight_pulse_state_t *state) {
    era_pulse_reset_runtime(state);
}

static inline bool era_backlight_pulse_press(era_backlight_pulse_state_t *state, uint8_t row, uint8_t col) {
    return era_pulse_press(state, row, col);
}

static inline bool era_backlight_pulse_release(era_backlight_pulse_state_t *state, uint8_t effect, uint8_t row, uint8_t col) {
    return era_pulse_release(state, era_backlight_pulse_hold_effect(effect), row, col);
}

static inline bool era_backlight_pulse_expire(era_backlight_pulse_state_t *state, uint8_t effect) {
    return era_pulse_expire(state, era_backlight_pulse_hold_effect(effect));
}

static inline void era_backlight_pulse_suspend(era_backlight_pulse_state_t *state) {
    era_pulse_suspend(state);
}

static inline void era_backlight_pulse_resume(era_backlight_pulse_state_t *state) {
    era_pulse_resume(state);
}

static inline uint8_t era_backlight_pulse_output_level(const era_backlight_pulse_state_t *state, uint8_t effect, uint8_t brightness) {
    return era_pulse_output_on(state, era_backlight_pulse_default_on(effect)) ? brightness : 0;
}
