// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The backlight effect engine.
 *
 * **The whole design is the answer to one question: what does this cost on a
 * matrix pass at 18-20 kHz.** The answer is one `volatile bool` load and a
 * branch, in every effect, whether or not a Pulse is running -- and that is
 * why the two obvious implementations are not used:
 *
 *  - **Not a busy wait.** The platform ships one already: `breathing_pulse()`
 *    (`platforms/chibios/drivers/backlight_pwm.c`) is
 *    set / `wait_ms(10)` / restore. Called from a key event that is a 10 ms
 *    stall of the keyboard loop -- roughly two hundred scan passes -- on every
 *    keypress the effect reacts to. Nothing in this tree calls it and nothing
 *    should.
 *  - **Not a deadline polled from the task.** Stamping `now + duration` and
 *    comparing against `timer_read32()` each pass reads the clock on every one
 *    of the thousands of passes a Pulse spans. The read is cached to a
 *    millisecond and still is not free, and while someone is typing the Pulse
 *    window is most of the time.
 *
 * What it does instead is what the breathing effect beside it already does:
 * hand the interval to a ChibiOS virtual timer and let the tick hardware keep
 * it. The key event writes the PWM once and arms a one-shot; the callback sets
 * only bounded flags; the next due pass consumes them and writes the PWM back.
 * No clock is read on the scan path at all, and the cost of an effect that is off is the
 * same as the cost of one that is running.
 *
 * `chVTSetI()` resets an armed timer before re-arming it (`chVTResetI` first),
 * so a key pressed during a pulse simply restarts the interval. The key-event
 * path retires any already-published expiry under that same system lock, so an
 * old completion cannot cut the fresh interval short.
 *
 * Ordering trap, and the reason the state is applied from the task rather than
 * from init: `matrix_init()` runs at position 13 of `keyboard_init()`
 * (`quantum/keyboard.c`) and `backlight_init_ports()` at 21, so this unit's
 * `init` happens **before** the PWM driver is started. Anything it wrote to the
 * hardware there would be overwritten at best and asserted at worst. The
 * pending flag it raises instead is consumed on the first pass, which cannot
 * run until `keyboard_init()` has returned.
 */

#include "era_backlight.h"
#include "era_backlight_pulse_policy.h"

#if !defined(PROTOCOL_CHIBIOS)
#    error "ERA backlight effects need the ChibiOS virtual timer; there is no busy-wait fallback and there will not be one."
#endif

#include <string.h>
#include "ch.h"
#include "backlight.h"
#include "../storage/era_eeprom_storage.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

enum {
    /* Pulse speed is 1..10 with 10 the fastest, which is the range the shipped
       definitions offer. The unit is chosen here because the definition states
       a range and not a duration: 20 ms a step puts the slowest pulse at
       200 ms and the fastest at 20 ms, which spans "clearly a pulse" to "just
       perceptible" without ever approaching a length a fast typist would
       notice as lag. */
    ERA_BACKLIGHT_PULSE_UNIT_MS = 20,
    ERA_BACKLIGHT_DEFAULT_SPEED = 5,
    /* Seconds per breath. QMK's `BREATHING_PERIOD` is the board's own default
       and is what a board with no stored config should start at. */
    ERA_BACKLIGHT_DEFAULT_PERIOD = BREATHING_PERIOD,
    /* Any byte but zero would do; a fresh EEPROM reads zero and must land on
       the defaults rather than on Steady with a zero period. */
    ERA_BACKLIGHT_CONFIG_VALID = 0xB1,
};

typedef struct __attribute__((packed)) {
    uint8_t valid;
    uint8_t effect;
    uint8_t breathing_period;
    uint8_t pulse_speed;
} era_backlight_config_t;

_Static_assert(sizeof(era_backlight_config_t) == ERA_EEPROM_BACKLIGHT_CONFIG_SIZE, "ERA backlight config size changed.");

static era_backlight_config_t backlight_config_era;

/* Written by the virtual-timer callback in interrupt context and by the task
   in thread context. One byte, one writer at a time, no read-modify-write on
   either side, so it needs volatility and nothing more. */
static volatile bool backlight_apply_due;
static volatile bool backlight_pulse_timer_due;

static virtual_timer_t backlight_pulse_vt;
static era_backlight_pulse_state_t backlight_pulse_state;

static uint8_t era_backlight_clamp(uint8_t value, uint8_t min, uint8_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static bool era_backlight_effect_supported(uint8_t effect) {
#if !defined(BACKLIGHT_BREATHING)
    /* A board can compile the backlight without the breathing table. The
       dropdown still offers the entry, so the refusal has to be here rather
       than in the definition, and it degrades to steady rather than to
       nothing. */
    if (effect == ERA_BACKLIGHT_EFFECT_BREATHING) {
        return false;
    }
#endif
    return effect < ERA_BACKLIGHT_EFFECT_COUNT;
}

static void era_backlight_apply_defaults(era_backlight_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->valid            = ERA_BACKLIGHT_CONFIG_VALID;
    config->effect           = ERA_BACKLIGHT_EFFECT_STEADY;
    config->breathing_period = ERA_BACKLIGHT_DEFAULT_PERIOD;
    config->pulse_speed      = ERA_BACKLIGHT_DEFAULT_SPEED;
}

static bool era_backlight_normalize(era_backlight_config_t *config) {
    era_backlight_config_t before = *config;

    if (!era_backlight_effect_supported(config->effect)) {
        config->effect = ERA_BACKLIGHT_EFFECT_STEADY;
    }
    config->breathing_period = era_backlight_clamp(config->breathing_period, ERA_BACKLIGHT_PERIOD_MIN, ERA_BACKLIGHT_PERIOD_MAX);
    config->pulse_speed      = era_backlight_clamp(config->pulse_speed, ERA_BACKLIGHT_SPEED_MIN, ERA_BACKLIGHT_SPEED_MAX);

    return memcmp(&before, config, sizeof(before)) != 0;
}

static uint8_t era_backlight_output_level(void) {
    if (era_backlight_pulse_effect(backlight_config_era.effect)) {
        return era_backlight_pulse_output_level(&backlight_pulse_state, backlight_config_era.effect, get_backlight_level());
    }
    return backlight_pulse_state.suspended ? 0 : get_backlight_level();
}

static void era_backlight_apply(void) {
    if (backlight_pulse_state.suspended) {
#if defined(BACKLIGHT_BREATHING)
        if (is_breathing()) {
            breathing_disable();
        }
#endif
        backlight_set(0);
        return;
    }
#if defined(BACKLIGHT_BREATHING)
    if (backlight_config_era.effect == ERA_BACKLIGHT_EFFECT_BREATHING) {
        breathing_period_set(backlight_config_era.breathing_period);
        if (!is_breathing()) {
            breathing_enable();
        }
        return;
    }
    if (is_breathing()) {
        /* Also restores the level, which the steady write below then confirms
           for Pulse On where the normal state is dark. */
        breathing_disable();
    }
#endif
    backlight_set(era_backlight_output_level());
}

/* Interrupt context. Two flag stores, and deliberately nothing else: the restore is a
   `pwmEnableChannel()` call, which is thread-context API, and reaching for its
   I-class twin would mean copying the driver's duty arithmetic into this
   layer. The task takes both flags under the ChibiOS system lock on the rare
   due path, so an expiry cannot be overwritten by a concurrent thread clear. */
static void era_backlight_pulse_expired(virtual_timer_t *vtp, void *p) {
    (void)vtp;
    (void)p;
    backlight_pulse_timer_due = true;
    backlight_apply_due       = true;
}

static bool era_backlight_load_from_eeprom(bool write_defaults) {
    bool dirty = false;

    if (era_eeprom_read_config(&backlight_config_era, ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, sizeof(backlight_config_era)) != sizeof(backlight_config_era) || backlight_config_era.valid != ERA_BACKLIGHT_CONFIG_VALID) {
        era_backlight_apply_defaults(&backlight_config_era);
        dirty = true;
    } else {
        dirty = era_backlight_normalize(&backlight_config_era);
    }

    if (dirty || write_defaults) {
        era_backlight_save_config();
    }

    chVTReset(&backlight_pulse_vt);
    era_backlight_pulse_reset_runtime(&backlight_pulse_state);
    backlight_pulse_timer_due = false;
    backlight_apply_due       = true;
    return true;
}

void era_backlight_init(void) {
    chVTObjectInit(&backlight_pulse_vt);
    memset(&backlight_pulse_state, 0, sizeof(backlight_pulse_state));
    era_backlight_load_from_eeprom(true);
}

void era_backlight_reload_from_eeprom(void) {
    era_backlight_load_from_eeprom(false);
}

void era_backlight_save_config(void) {
    era_eeprom_update_config(&backlight_config_era, ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, sizeof(backlight_config_era));
}

void era_backlight_task(void) {
    if (!backlight_apply_due) {
        return;
    }

    /* Keep the one-load/one-branch fast path above. Only a due pass pays the
       system lock. Virtual-timer callbacks run in interrupt context, so this is
       the point that atomically takes ownership of an expiry flag and the apply
       flag it armed. */
    chSysLock();
    bool pulse_timer_due       = backlight_pulse_timer_due;
    backlight_pulse_timer_due  = false;
    backlight_apply_due        = false;
    chSysUnlock();

    if (pulse_timer_due) {
        era_backlight_pulse_expire(&backlight_pulse_state, backlight_config_era.effect);
    }
    era_backlight_apply();
}

bool era_backlight_process_record(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;

    /* Per key event, not per matrix pass. Non-Pulse modes pay only this bounded
       range test. Hold adds one counter update on the same event path; no clock
       read or matrix walk is introduced. */
    if (!era_backlight_pulse_effect(backlight_config_era.effect) || backlight_pulse_state.suspended) {
        return true;
    }
    if (record == NULL) {
        return true;
    }

    if (!record->event.pressed) {
        if (era_backlight_pulse_release(&backlight_pulse_state, backlight_config_era.effect)) {
            backlight_apply_due = true;
        }
        return true;
    }

    era_backlight_pulse_press(&backlight_pulse_state);
    backlight_set(era_backlight_output_level());
    /* Retire an expiry the previous pulse may already have published but the
       task has not consumed yet, then re-arm under the same ChibiOS system
       lock. `chVTSetI()` resets an armed timer first. A press that lands on the
       expiry/task boundary therefore starts one fresh interval rather than
       inheriting the old pulse's pending completion. This is key-event-only
       O(1) work; the matrix-pass fast path remains untouched. */
    chSysLock();
    backlight_pulse_timer_due = false;
    chVTSetI(&backlight_pulse_vt, TIME_MS2I((uint16_t)(ERA_BACKLIGHT_SPEED_MAX + 1 - backlight_config_era.pulse_speed) * ERA_BACKLIGHT_PULSE_UNIT_MS), era_backlight_pulse_expired, NULL);
    chSysUnlock();
    return true;
}

void era_backlight_suspend(void) {
    chVTReset(&backlight_pulse_vt);
    backlight_pulse_timer_due = false;
    era_backlight_pulse_suspend(&backlight_pulse_state);
    backlight_apply_due = false;
#if defined(BACKLIGHT_BREATHING)
    /* QMK's generic suspend writes backlight level zero but does not stop the
       ChibiOS breathing virtual timer. Cancel it here before the generic path
       darkens the PWM rail, otherwise its ISR can enable the channel again
       while the host is asleep. `breathing_disable()` briefly restores the
       live level; `suspend_power_down_quantum()` writes zero immediately after
       this kb hook returns. */
    if (is_breathing()) {
        breathing_disable();
    }
#endif
}

void era_backlight_resume(void) {
    chVTReset(&backlight_pulse_vt);
    backlight_pulse_timer_due = false;
    era_backlight_pulse_resume(&backlight_pulse_state);
    /* QMK has already restored its stored brightness before the kb wake hook.
       Apply immediately so Pulse On modes do not flash their normally-dark
       rail for a whole keyboard pass. */
    era_backlight_apply();
    backlight_apply_due = false;
}

void era_backlight_refresh_output(void) {
    /* Runtime-only caller: VIA/keycode brightness setters have already touched
       QMK's PWM output, so restore Pulse polarity immediately rather than
       exposing a one-scan flash of that raw level. A pending timer expiry, if
       any, still owns the next task transition. */
    era_backlight_apply();
}

uint8_t era_backlight_get_effect(void) {
    return backlight_config_era.effect;
}

void era_backlight_set_effect(uint8_t effect) {
    if (!era_backlight_effect_supported(effect)) {
        return;
    }
    if (backlight_config_era.effect == effect) {
        return;
    }
    /* A mode switch retires the complete runtime pulse state. Keys that were
       already held before the new mode do not synthesize a new press. */
    chVTReset(&backlight_pulse_vt);
    era_backlight_pulse_reset_runtime(&backlight_pulse_state);
    backlight_pulse_timer_due   = false;
    backlight_config_era.effect = effect;
    backlight_apply_due         = true;
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, sizeof(backlight_config_era));
#endif
}

uint8_t era_backlight_get_breathing_period(void) {
    return backlight_config_era.breathing_period;
}

void era_backlight_set_breathing_period(uint8_t period) {
    uint8_t next = era_backlight_clamp(period, ERA_BACKLIGHT_PERIOD_MIN, ERA_BACKLIGHT_PERIOD_MAX);
    if (backlight_config_era.breathing_period == next) {
        return;
    }
    backlight_config_era.breathing_period = next;
    backlight_apply_due                   = true;
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, sizeof(backlight_config_era));
#endif
}

uint8_t era_backlight_get_pulse_speed(void) {
    return backlight_config_era.pulse_speed;
}

void era_backlight_set_pulse_speed(uint8_t speed) {
    /* No apply: the next pulse reads the new speed, and a pulse already in
       flight keeps the interval it was armed with. */
    uint8_t next = era_backlight_clamp(speed, ERA_BACKLIGHT_SPEED_MIN, ERA_BACKLIGHT_SPEED_MAX);
    if (backlight_config_era.pulse_speed == next) {
        return;
    }
    backlight_config_era.pulse_speed = next;
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, sizeof(backlight_config_era));
#endif
}
