// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The one Pulse state machine both keypress-reactive lighting families run.
 *
 * Pure transitions, clock-free, no lighting engine named: the production units
 * (`era_backlight.c` for the PWM rail, `era_rgblight_pulse.c` for the underglow
 * chain) supply the ChibiOS one-shot and the hardware write, and the host
 * tests drive these functions directly. Keeping the family behaviour in one
 * header is what makes "the same Pulse" a fact rather than a resemblance: a
 * change to how overlapping keys hold, or to when an expiry restores, lands in
 * both units at once.
 *
 * The four effects of either family are two orthogonal bits. `default_on` is
 * the resting output: Off Press rests lit and blanks on a press, On Press
 * rests dark and lights on a press. `hold` keeps the pulse past its interval
 * while any key pressed inside it is still down, and restores on the last
 * release. */

/* The pulse width both families arm, from the ordinary 0..255 VIA speed
   value: 5 ms at 0, 20 ms at the boards' default 15, 260 ms at 255. The 5 ms
   floor and 260 ms ceiling define the requested interval. Physical LED
   visibility also depends on the driver and LED device. One control and one
   width for the PWM rail and the underglow chain. */
enum {
    ERA_PULSE_MIN_MS  = 5,
    ERA_PULSE_STEP_MS = 1,
};

static inline uint16_t era_pulse_duration_ms(uint8_t speed) {
    return (uint16_t)ERA_PULSE_MIN_MS + (uint16_t)speed * (uint16_t)ERA_PULSE_STEP_MS;
}

typedef struct {
    bool    active;        /* the output is inverted from its resting state */
    bool    timer_expired; /* the one-shot for the current pulse has completed */
    bool    suspended;     /* lighting sleep owns the output; presses are ignored */
    uint16_t pressed_count; /* keys admitted in this mode and still down */
    /* A release from a key held across mode/sleep reset must not release a
       newer key's Hold. One bit per matrix position keeps that decision O(1). */
    uint8_t pressed_keys[(MATRIX_ROWS * MATRIX_COLS + 7U) / 8U];
} era_pulse_state_t;

static inline void era_pulse_reset_runtime(era_pulse_state_t *state) {
    state->active        = false;
    state->timer_expired = false;
    state->pressed_count = 0;
    memset(state->pressed_keys, 0, sizeof(state->pressed_keys));
}

/* A press starts or restarts the pulse. The caller re-arms its one-shot. */
static inline bool era_pulse_press(era_pulse_state_t *state, uint8_t row, uint8_t col) {
    if (state->suspended || row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return false;
    }
    uint16_t key = (uint16_t)row * MATRIX_COLS + col;
    uint8_t mask = (uint8_t)(1U << (key & 7U));
    if (state->pressed_keys[key >> 3] & mask) {
        return false;
    }
    state->pressed_keys[key >> 3] |= mask;
    state->pressed_count++;
    state->active        = true;
    state->timer_expired = false;
    return true;
}

/* True when the release itself ends the pulse: a Hold effect whose interval
   already expired and whose last held key just went up. */
static inline bool era_pulse_release(era_pulse_state_t *state, bool hold, uint8_t row, uint8_t col) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return false;
    }
    uint16_t key = (uint16_t)row * MATRIX_COLS + col;
    uint8_t mask = (uint8_t)(1U << (key & 7U));
    if (!(state->pressed_keys[key >> 3] & mask)) {
        return false;
    }
    state->pressed_keys[key >> 3] &= (uint8_t)~mask;
    state->pressed_count--;
    if (hold && state->timer_expired && state->pressed_count == 0 && state->active) {
        state->active = false;
        return true;
    }
    return false;
}

/* True when the expiry ends the pulse. A Hold effect with a key still down
   keeps it, and the release above finishes it. */
static inline bool era_pulse_expire(era_pulse_state_t *state, bool hold) {
    state->timer_expired = true;
    if (!hold || state->pressed_count == 0) {
        bool changed  = state->active;
        state->active = false;
        return changed;
    }
    return false;
}

static inline void era_pulse_suspend(era_pulse_state_t *state) {
    era_pulse_reset_runtime(state);
    state->suspended = true;
}

static inline void era_pulse_resume(era_pulse_state_t *state) {
    state->suspended = false;
    era_pulse_reset_runtime(state);
}

/* The output the state asks for: the resting level, inverted while active,
   and dark while suspended. */
static inline bool era_pulse_output_on(const era_pulse_state_t *state, bool default_on) {
    if (state->suspended) {
        return false;
    }
    return state->active ? !default_on : default_on;
}
