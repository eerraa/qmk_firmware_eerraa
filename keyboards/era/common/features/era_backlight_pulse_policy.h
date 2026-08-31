// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_backlight.h"

/* Pure state transition layer for the Pulse effects. The production engine
 * supplies the ChibiOS one-shot timer and PWM writes; keeping these transitions
 * clock-free makes the overlap/hold semantics deterministic in host tests and
 * keeps the matrix-pass hot path unchanged. */
typedef struct {
    bool    active;
    bool    timer_expired;
    bool    suspended;
    uint8_t pressed_count;
} era_backlight_pulse_state_t;

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
    state->active        = false;
    state->timer_expired = false;
    state->pressed_count = 0;
}

static inline void era_backlight_pulse_press(era_backlight_pulse_state_t *state) {
    if (state->suspended) {
        return;
    }
    if (state->pressed_count != UINT8_MAX) {
        state->pressed_count++;
    }
    state->active        = true;
    state->timer_expired = false;
}

static inline bool era_backlight_pulse_release(era_backlight_pulse_state_t *state, uint8_t effect) {
    if (state->pressed_count > 0) {
        state->pressed_count--;
    }
    if (era_backlight_pulse_hold_effect(effect) && state->timer_expired && state->pressed_count == 0 && state->active) {
        state->active = false;
        return true;
    }
    return false;
}

static inline bool era_backlight_pulse_expire(era_backlight_pulse_state_t *state, uint8_t effect) {
    state->timer_expired = true;
    if (!era_backlight_pulse_hold_effect(effect) || state->pressed_count == 0) {
        bool changed = state->active;
        state->active = false;
        return changed;
    }
    return false;
}

static inline void era_backlight_pulse_suspend(era_backlight_pulse_state_t *state) {
    era_backlight_pulse_reset_runtime(state);
    state->suspended = true;
}

static inline void era_backlight_pulse_resume(era_backlight_pulse_state_t *state) {
    state->suspended = false;
    era_backlight_pulse_reset_runtime(state);
}

static inline uint8_t era_backlight_pulse_output_level(const era_backlight_pulse_state_t *state, uint8_t effect, uint8_t brightness) {
    if (state->suspended) {
        return 0;
    }
    bool on = era_backlight_pulse_default_on(effect);
    if (state->active) {
        on = !on;
    }
    return on ? brightness : 0;
}
