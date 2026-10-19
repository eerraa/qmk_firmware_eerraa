// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The underglow Pulse engine. The design is `era_backlight.c`'s, and that
 * file's header comment is the reasoning; this one names the differences.
 *
 *  - **The write is not on the key event.** A PWM level is one register; an
 *    underglow frame is the whole chain through the WS2812 driver. The switch
 *    edge therefore records the policy transition and arms the one-shot, and
 *    the next RGBLight animation tick -- scheduled at 1 ms while the
 *    keyboard loop runs -- writes the LEDs. That keeps the
 *    matrix scan's switch-event fanout at bounded state, as
 *    `era_invariants.md` asks of anything on that path.
 *  - **Nothing is added to the per-pass path.** The tick already exists for
 *    every dynamic RGBLight effect; the engine is only ever called from it.
 *  - **The tick writes on change only.** It compares the output the policy
 *    asks for and the configured colour against what it last wrote; a chain
 *    flush per millisecond for an unchanged frame would be the one thing a
 *    reactive effect must not cost.
 *  - **Reset happens at the mode change.** `rgblight_mode_eeprom_helper()`
 *    in `quantum/rgblight/rgblight.c` retires the old state synchronously.
 *    Deferring that reset to the first animation tick would erase physical
 *    presses arriving between selection and that tick. The same core file
 *    owns enable/disable and actual lighting sleep, including the Sleep
 *    master's decision to leave RGB running during USB suspend.
 */

#include "era_rgblight_pulse.h"
#include "era_pulse_policy.h"

#if !defined(PROTOCOL_CHIBIOS)
#    error "ERA RGBLight Pulse needs the ChibiOS virtual timer; there is no busy-wait fallback and there will not be one."
#endif

#include <string.h>
#include "ch.h"

/* Written by the virtual-timer callback in interrupt context and cleared by
   the animation tick in thread context under the system lock; one byte, no
   read-modify-write on either side, so volatile and nothing more. */
static volatile bool rgblight_pulse_timer_due;

static virtual_timer_t   rgblight_pulse_vt;
static era_pulse_state_t rgblight_pulse_state;

/* What the chain currently shows, so the tick writes only on a change.
   `valid` is dropped wherever RGBLight may have written the LEDs itself:
   mode entry, suspend, wake. */
static struct {
    bool    valid;
    bool    on;
    uint8_t hue;
    uint8_t sat;
    uint8_t val;
} rgblight_pulse_applied;

bool era_rgblight_pulse_mode(uint8_t mode) {
    return mode >= RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS && mode <= RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD;
}

static bool era_rgblight_pulse_hold(uint8_t mode) {
    return mode == RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD || mode == RGBLIGHT_MODE_ERA_PULSE_ON_PRESS_HOLD;
}

static bool era_rgblight_pulse_default_on(uint8_t mode) {
    return mode == RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS || mode == RGBLIGHT_MODE_ERA_PULSE_OFF_PRESS_HOLD;
}

uint16_t era_rgblight_pulse_duration_ms(void) {
    return era_pulse_duration_ms(rgblight_get_speed());
}

/* The engine owns the chain only while RGBLight is on and a Pulse mode is
   selected; All Off, another effect and lighting sleep all leave it inert. */
static bool era_rgblight_pulse_live(void) {
    return rgblight_is_enabled() && era_rgblight_pulse_mode(rgblight_get_mode());
}

/* Interrupt context: one flag store, and nothing else, for the reason
   `era_backlight.c` gives at its callback. */
static void era_rgblight_pulse_expired(virtual_timer_t *vtp, void *p) {
    (void)vtp;
    (void)p;
    rgblight_pulse_timer_due = true;
}

void era_rgblight_pulse_reset(void) {
    chVTReset(&rgblight_pulse_vt);
    era_pulse_reset_runtime(&rgblight_pulse_state);
    rgblight_pulse_timer_due     = false;
    rgblight_pulse_applied.valid = false;
}

void era_rgblight_pulse_init(void) {
    chVTObjectInit(&rgblight_pulse_vt);
    memset(&rgblight_pulse_state, 0, sizeof(rgblight_pulse_state));
    memset(&rgblight_pulse_applied, 0, sizeof(rgblight_pulse_applied));
}

void era_rgblight_pulse_refresh(void) {
    /* A native RGBLight layer changed. Rebuild the base before the current
       overlays are applied, without disturbing a held pulse or its timer. */
    rgblight_pulse_applied.valid = false;
}

void era_rgblight_pulse_note_key_event(uint8_t row, uint8_t col, bool pressed) {
    /* A physical switch edge, like the backlight's: the effect answers the
       key, not the keycode QMK later settles it into. */
    if (!era_rgblight_pulse_live() || rgblight_pulse_state.suspended) {
        return;
    }

    if (!pressed) {
        /* A release that ends a Hold pulse changes the requested output; the
           next tick sees that through the policy and writes the chain. */
        (void)era_pulse_release(&rgblight_pulse_state, era_rgblight_pulse_hold(rgblight_get_mode()), row, col);
        return;
    }

    if (!era_pulse_press(&rgblight_pulse_state, row, col)) {
        return;
    }
    /* Retire an expiry the previous pulse may already have published but the
       tick has not consumed, then re-arm under the same system lock, so a
       press on the expiry/tick boundary starts one fresh interval rather than
       inheriting the old pulse's completion. `chVTSetI()` resets an armed
       timer first. */
    chSysLock();
    rgblight_pulse_timer_due = false;
    chVTSetI(&rgblight_pulse_vt, TIME_MS2I(era_rgblight_pulse_duration_ms()), era_rgblight_pulse_expired, NULL);
    chSysUnlock();
}

void era_rgblight_pulse_effect(animation_status_t *anim) {
    (void)anim;
    uint8_t mode = rgblight_get_mode();
    if (!era_rgblight_pulse_live() || rgblight_pulse_state.suspended) {
        return;
    }

    if (rgblight_pulse_timer_due) {
        chSysLock();
        bool due                 = rgblight_pulse_timer_due;
        rgblight_pulse_timer_due = false;
        chSysUnlock();
        if (due) {
            (void)era_pulse_expire(&rgblight_pulse_state, era_rgblight_pulse_hold(mode));
        }
    }

    bool    on  = era_pulse_output_on(&rgblight_pulse_state, era_rgblight_pulse_default_on(mode));
    uint8_t hue = rgblight_get_hue();
    uint8_t sat = rgblight_get_sat();
    uint8_t val = rgblight_get_val();
    if (rgblight_pulse_applied.valid && rgblight_pulse_applied.on == on && rgblight_pulse_applied.hue == hue && rgblight_pulse_applied.sat == sat && rgblight_pulse_applied.val == val) {
        return;
    }

    if (on) {
        rgblight_sethsv_range(hue, sat, val, rgblight_ranges.effect_start_pos, rgblight_ranges.effect_end_pos);
    } else {
        rgblight_setrgb(0, 0, 0);
    }
    rgblight_pulse_applied.valid = true;
    rgblight_pulse_applied.on    = on;
    rgblight_pulse_applied.hue   = hue;
    rgblight_pulse_applied.sat   = sat;
    rgblight_pulse_applied.val   = val;
}

void era_rgblight_pulse_suspend(void) {
    chVTReset(&rgblight_pulse_vt);
    rgblight_pulse_timer_due = false;
    era_pulse_suspend(&rgblight_pulse_state);
    rgblight_pulse_applied.valid = false;
}

void era_rgblight_pulse_resume(void) {
    chVTReset(&rgblight_pulse_vt);
    rgblight_pulse_timer_due = false;
    era_pulse_resume(&rgblight_pulse_state);
    /* `rgblight_wakeup()` re-enters the mode; the next tick renders it. */
    rgblight_pulse_applied.valid = false;
}
