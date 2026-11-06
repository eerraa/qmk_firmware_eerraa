// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include "era_macro.h"
#include "action.h"
#include "action_util.h"
#include "action_output.h"
#include "dynamic_keymap.h"
#include "send_string.h"
#include "timer.h"
#include "host.h"
#include "eeprom.h"
#ifdef MOUSEKEY_ENABLE
#    include "mousekey.h"
#endif
#ifndef DYNAMIC_KEYMAP_MACRO_DELAY
#    define DYNAMIC_KEYMAP_MACRO_DELAY TAP_CODE_DELAY
#endif
#ifndef DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE
#    ifdef NVM_DRIVER_EEPROM
#        include "nvm/eeprom/nvm_dynamic_keymap_geometry.h"
#    else
/* A custom NVM owner supplies its own size or keeps the existing upper bound. */
#        define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE TOTAL_EEPROM_BYTE_COUNT
#    endif
#endif
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE <= UINT16_MAX, "Macro snapshot index width");
/* One snapshot and one admitted macro; there is no pending queue. The AVR
 * board does not select this module. No heap and no storage access on idle or
 * active execution. */
static uint8_t snapshot[DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE];
static uint16_t cursor, limit, origin;
static bool active, waiting, known, valid, paused;
/* A report/replay obligation, not merely currently held usages. The final
 * UP may have changed host_last_* while a concurrent USB reset refused its
 * restored physical output. Keep this until cancel retires and replays it. */
static bool outputs;
static uint32_t generation, stamp, remaining;
static uint8_t key;
static bool shifted, altgr, dead;
typedef enum { READ, INTERVAL, TAP_UP, CHAR_SHIFT, CHAR_ALTGR, CHAR_TAP,
               CHAR_UP, CHAR_ALTGR_UP, CHAR_SHIFT_UP, CHAR_DEAD, CHAR_DEAD_UP } phase_t;
static phase_t phase;
static uint16_t after_tap_interval;

/* The admitted macro. `playback` is its nonzero identity from admission to its
 * end; STARTING is `playback && !active`, holding the ID until the task's one
 * snapshot. `flags` and `hold_ms` are the options taken at admission, with the
 * judging rules already applied, so a later SET changes only the next
 * playback. */
#define FLAG(name) ERA_WIRE_MACRO_OPTIONS_FLAGS_##name
static uint8_t playback, serial, macro_id, flags;
static uint8_t plays_left;
static uint16_t hold_ms;
/* Ingress's view of every matrix position, kept while idle too: whether it is
 * down, and the identity of its latest press (never 0). A record names the
 * physical press it stands for by that identity, so admission can tell
 * whether that press has already ended. */
#define POSITIONS (MATRIX_ROWS * MATRIX_COLS)
static uint8_t press_serial[POSITIONS];
static uint8_t down[(POSITIONS + 7) / 8];
/* A blocked press owns its release even after stop/cancel/completion. */
static uint8_t blocked[(POSITIONS + 7) / 8];
/* Stop on release follows the starting press until ingress sees it end. Stop
 * candidates -- presses of a key bound to the playing macro begun during
 * playback -- are at most one per position, so every one is kept, each with
 * its press time; `earliest` is the oldest's, the first to reach the hold
 * time. Both end with their playback. */
static bool trigger_armed;
static uint16_t trigger;
static uint8_t candidate[(POSITIONS + 7) / 8];
static uint32_t candidate_pressed[POSITIONS], earliest;
static uint16_t candidate_count;

static bool slot_at(uint8_t row, uint8_t col, uint16_t *slot) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;
    *slot = (uint16_t)(row * MATRIX_COLS + col);
    return true;
}
static bool bit_test(const uint8_t *bits, uint16_t slot) {
    return (bits[slot >> 3] >> (slot & 7)) & 1U;
}
static void bit_put(uint8_t *bits, uint16_t slot, bool on) {
    const uint8_t mask = (uint8_t)(1U << (slot & 7));
    bits[slot >> 3] = on ? (uint8_t)(bits[slot >> 3] | mask) : (uint8_t)(bits[slot >> 3] & ~mask);
}

/* The executor owns the meaning/lifetime of its two registered contributions. */
static action_output_owner_t macro_owner(bool temporary) {
    return temporary ? ACTION_OUTPUT_OWNER_MACRO_TAP : ACTION_OUTPUT_OWNER_MACRO_HOLD;
}
static void macro_clear(bool temporary) {
    action_output_owner_t owner = macro_owner(temporary);
    action_output_clear_owner_keys(owner);
    action_output_clear_owner_mods(owner);
    action_output_clear_owner_hid(owner);
}
static void wait_for(uint32_t delay) {
    if (!active) return;
    remaining = delay;
    stamp = timer_read32();
    waiting = delay != 0;
}
static void elapsed(uint32_t now) {
    if (!waiting) return;
    uint32_t delta = now - stamp;
    stamp = now;
    if (delta >= remaining) waiting = false;
    else remaining -= delta;
}
static void edge(uint8_t code, bool down, bool temporary) {
    uint8_t previous = action_output_set_owner(macro_owner(temporary));
#ifdef ERA_MACRO_OPTIONS_ENABLE
    bool suppression = action_output_set_mod_suppression((flags & FLAG(IGNORE_MODS)) != 0);
#endif
    outputs = true;
    if (down) register_code(code);
    else unregister_code(code);
#ifdef ERA_MACRO_OPTIONS_ENABLE
    action_output_set_mod_suppression(suppression);
#endif
    action_output_set_owner(previous);
}
static void end_playback(void) {
    active = waiting = false;
    playback = 0;
    trigger_armed = false;
    if (candidate_count) {
        memset(candidate, 0, sizeof(candidate));
        candidate_count = 0;
    }
}
/* Also every stop: a stop releases exactly what a cancel does, and never
 * restarts the macro. */
void era_macro_cancel(void) {
    end_playback();
    if (!outputs) return;
    macro_clear(true);
    macro_clear(false);
    outputs = false;
    send_keyboard_report();
    /* Old-generation intermediate releases were deliberately refused by the
     * transport. Reconstruct the final surviving owners without a synthetic
     * zero-to-held pulse or the host layer's duplicate suppression. */
#ifdef EXTRAKEY_ENABLE
    host_resend_last_extra_reports();
#endif
#ifdef MOUSEKEY_ENABLE
    report_mouse_t report = {.buttons = mousekey_get_report().buttons};
    host_mouse_send(&report);
#endif
}
void era_macro_session(uint32_t current, bool available, bool suspended) {
    if ((known && current != generation) || (valid && !available)) era_macro_cancel();
    known = true;
    generation = current;
    valid = available;
    if (suspended != paused) {
        uint32_t now = timer_read32();
        if (suspended) elapsed(now);
        else stamp = now;
        paused = suspended;
    }
}
bool era_macro_session_current(uint32_t current) {
    return !outputs || (known && generation == current);
}
/* One macro at a time: while one is admitted, a request is refused, never
 * deferred, because a queued macro would play late and leave every stop
 * judgment unsure which macro it ends. */
static bool admit(uint8_t id, bool keyed, uint16_t slot, uint8_t press) {
    era_macro_platform_sync();
    if (!valid || playback || id >= dynamic_keymap_macro_get_count()) return false;
    uint8_t options = 0;
    uint8_t count = 1;
    uint16_t hold = 0;
#ifdef ERA_MACRO_OPTIONS_ENABLE
    options = era_macro_options_flags(id);
    hold = era_macro_options_hold_ms(id);
    count = era_macro_options_repeat_count(id);
#endif
    /* With Stop on release one release rule decides; without a starting press
     * that ingress follows (an API start, a position outside the matrix) it
     * has nothing to judge; until-stopped with no stop condition plays once. */
    if (options & FLAG(RELEASE)) options &= (uint8_t)~(FLAG(RETAP) | FLAG(HOLD));
    if (!keyed) options &= (uint8_t)~FLAG(RELEASE);
    if (!count && !(options & (FLAG(RELEASE) | FLAG(RETAP) | FLAG(HOLD) | FLAG(OTHER)))) count = 1;
    plays_left = count;
    flags = options;
    hold_ms = hold;
    macro_id = id;
    if (!++serial) serial = 1;
    playback = serial;
    if (flags & FLAG(RELEASE)) {
        /* A starting press that already ended -- its release waited behind a
         * tapping decision, or decided the dance that started this macro --
         * has made its stop: the macro ends before it plays. */
        if (bit_test(down, slot) && press_serial[slot] == press) {
            trigger_armed = true;
            trigger = slot;
        } else {
            era_macro_cancel();
        }
    }
    return true;
}
bool era_macro_request(uint8_t id) {
    return admit(id, false, 0, 0);
}
bool era_macro_request_key(uint8_t id, uint8_t row, uint8_t col, uint8_t origin, uint8_t press) {
    /* Only a record that may start one: a press begun with no macro admitted,
     * or a dance's output when every press of the dance was. A press begun
     * during a playback never starts a macro, even when its deferred record
     * is processed after that playback ended. */
    if (!(origin & ERA_MACRO_ORIGIN_START)) return false;
    uint16_t slot = 0;
    const bool keyed = press && slot_at(row, col, &slot);
    return admit(id, keyed, slot, press);
}
/* The oldest remaining candidate's press time, needed only when the oldest left. */
static uint32_t oldest_candidate(void) {
    const uint32_t now = timer_read32();
    uint32_t oldest = now;
    for (uint16_t byte = 0; byte < sizeof(candidate); ++byte) {
        for (uint8_t i = 0; candidate[byte] >> i; ++i) {
            const uint16_t slot = (uint16_t)(byte * 8U + i);
            if (bit_test(candidate, slot) && now - candidate_pressed[slot] > now - oldest) oldest = candidate_pressed[slot];
        }
    }
    return oldest;
}
/* The press of candidate `slot` ended; true if that stopped the macro. */
static bool candidate_end(uint16_t slot) {
    const uint32_t pressed = candidate_pressed[slot];
    bit_put(candidate, slot, false);
    --candidate_count;
    /* Re-tap stops on a release before the hold time, or on any release
     * without Stop on hold; a hold time already reached is Stop on hold. */
    if ((flags & FLAG(RETAP)) || ((flags & FLAG(HOLD)) && timer_read32() - pressed >= hold_ms)) {
        era_macro_cancel();
        return true;
    }
    if (candidate_count && pressed == earliest) earliest = oldest_candidate();
    return false;
}
uint8_t era_macro_input(uint8_t row, uint8_t col, bool pressed, uint16_t keycode, uint8_t *press) {
    /* Taken before any judgment below can end the playback, so a press that
     * stops a macro still never starts one. */
    uint8_t origin = playback ? 0U : ERA_MACRO_ORIGIN_START;
    uint16_t slot = 0;
    const bool tracked = slot_at(row, col, &slot);
    *press = 0;
    if (tracked) {
        /* Decide before a stop can retire playback. A release uses the press's
         * decision, never the current flags or the release-time key mapping. */
        const bool drop = pressed ? playback && (flags & FLAG(BLOCK_OTHERS)) && keycode != QK_MACRO + macro_id : bit_test(blocked, slot);
        bit_put(blocked, slot, pressed && drop);
        if (drop) origin = ERA_MACRO_ORIGIN_BLOCKED;
        if (pressed) press_serial[slot] = (uint8_t)(press_serial[slot] % 255U + 1U);
        bit_put(down, slot, pressed);
        *press = press_serial[slot];
        /* Any edge at a followed position ends the press held there: its
         * release, or the next press when its release took a path ingress
         * does not see (a captured wake key's). */
        if (trigger_armed && slot == trigger) {
            trigger_armed = false;
            era_macro_cancel();
            return origin;
        }
        if (bit_test(candidate, slot) && candidate_end(slot)) return origin;
    }
    if (!playback || !pressed) return origin;
    /* Only presses begun during playback reach here: the starting press was
     * admitted later, so it never counts. */
    if (keycode != QK_MACRO + macro_id) {
        if (flags & FLAG(OTHER)) era_macro_cancel();
    } else if (tracked && (flags & (FLAG(RETAP) | FLAG(HOLD)))) {
        candidate_pressed[slot] = timer_read32();
        bit_put(candidate, slot, true);
        if (!candidate_count++) earliest = candidate_pressed[slot];
    }
    return origin;
}
static bool hold_reached(void) {
    return (flags & FLAG(HOLD)) && candidate_count && timer_read32() - earliest >= hold_ms;
}
static bool start(uint8_t id) {
    uint16_t size = dynamic_keymap_macro_get_buffer_size();
    if (!size || size > sizeof(snapshot)) return false;
    dynamic_keymap_macro_get_buffer(0, size, snapshot);
    if (snapshot[size - 1U]) return false;
    uint16_t at = 0;
    /* The final marker is also the final macro's allowed terminator. */
    while (id && at < size) if (!snapshot[at++]) --id;
    if (id || at >= size) return false;
    origin = cursor = at;
    limit = size;
    active = true;
    waiting = false;
    phase = READ;
    return true;
}
static uint8_t read_byte(void) { return cursor < limit ? snapshot[cursor++] : 0; }
static void finish(bool malformed) {
    /* Ordinary completion keeps an explicit DOWN held, as stock QMK does. */
    macro_clear(true);
    if (malformed) macro_clear(false);
    send_keyboard_report();
    end_playback();
}
static void tap_begin(uint8_t code, uint16_t dwell, phase_t next) {
    key = code;
    edge(code, true, true);
    if (!active) return;
    phase = next;
    /* Match ERA's existing transport interval for keyboard taps. The UP is
     * still deferred to a later phase; other HID pages use a cooperative dwell. */
    if (IS_BASIC_KEYCODE(code) || IS_MODIFIER_KEYCODE(code)) {
        if (dwell) host_keyboard_delay(dwell);
    }
    else wait_for(dwell);
}
static bool command(void) {
    uint8_t cmd = read_byte();
    if (!cmd) return false;
    if (cmd == SS_TAP_CODE || cmd == SS_DOWN_CODE || cmd == SS_UP_CODE) {
        uint8_t code = read_byte();
        if (!code) return false;
        if (cmd == SS_TAP_CODE) {
            after_tap_interval = DYNAMIC_KEYMAP_MACRO_DELAY;
            tap_begin(code, code == KC_CAPS_LOCK ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY, TAP_UP);
        } else {
            edge(code, cmd == SS_DOWN_CODE, false);
            wait_for(DYNAMIC_KEYMAP_MACRO_DELAY);
        }
    } else if (cmd == SS_DELAY_CODE) {
        uint32_t delay = 0;
        /* VIA encodes 1..9999 decimal milliseconds followed by '|'. Refuse
         * truncated/overflowing data without an unbounded parser loop. */
        for (uint8_t digits = 0; ; ++digits) {
            uint8_t c = read_byte();
            if (c == '|') break;
            if (digits == 4 || c < '0' || c > '9') return false;
            delay = delay * 10U + c - '0';
        }
        phase = INTERVAL;
        wait_for(delay);
    } else {
        wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); /* QMK ignores unknown commands. */
    }
    return true;
}
void era_macro_task(void) {
    if (!valid || paused || !playback) return;
    /* Before the delay early-return, so a long DELAY cannot outlast Stop on hold. */
    if (hold_reached()) {
        era_macro_cancel();
        return;
    }
    if (era_macro_platform_pressured()) return;
    if (waiting) {
        elapsed(timer_read32());
        if (waiting) return;
    }
    /* STARTING: at most one bounded snapshot/selection per housekeeping pass. */
    if (!active && !start(macro_id)) {
        end_playback();
        return;
    }
    for (uint8_t budget = 0; budget < 16 && active && !waiting; ++budget) {
        if (era_macro_platform_pressured()) return;
        switch (phase) {
            case READ: {
                uint8_t ascii = read_byte();
                if (!ascii && plays_left != 1) {
                    /* A repeat boundary releases every macro-held key and
                     * publishes, then replays the same snapshot next pass. */
                    macro_clear(true);
                    macro_clear(false);
                    send_keyboard_report();
                    cursor = origin;
                    if (plays_left) --plays_left;
                    return;
                }
                if (!ascii) { finish(false); break; }
                if (ascii == SS_QMK_PREFIX) { if (!command()) finish(true); break; }
                if (ascii >= 128) { finish(true); break; }
                key = pgm_read_byte(&ascii_to_keycode_lut[ascii]);
                shifted = (pgm_read_byte(&ascii_to_shift_lut[ascii / 8]) >> (ascii % 8)) & 1;
                altgr = (pgm_read_byte(&ascii_to_altgr_lut[ascii / 8]) >> (ascii % 8)) & 1;
                dead = (pgm_read_byte(&ascii_to_dead_lut[ascii / 8]) >> (ascii % 8)) & 1;
                phase = CHAR_SHIFT;
                break;
            }
            case INTERVAL: phase = READ; wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); break;
            case TAP_UP: edge(key, false, true); phase = READ; wait_for(after_tap_interval); break;
            case CHAR_SHIFT:
                phase = CHAR_ALTGR;
                if (shifted) { edge(KC_LEFT_SHIFT, true, true); wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); }
                break;
            case CHAR_ALTGR:
                phase = CHAR_TAP;
                if (altgr) { edge(KC_RIGHT_ALT, true, true); wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); }
                break;
            case CHAR_TAP: tap_begin(key, DYNAMIC_KEYMAP_MACRO_DELAY, CHAR_UP); break;
            case CHAR_UP: edge(key, false, true); phase = CHAR_ALTGR_UP; wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); break;
            case CHAR_ALTGR_UP:
                phase = CHAR_SHIFT_UP;
                if (altgr) { edge(KC_RIGHT_ALT, false, true); wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); }
                break;
            case CHAR_SHIFT_UP:
                phase = CHAR_DEAD;
                if (shifted) { edge(KC_LEFT_SHIFT, false, true); wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); }
                break;
            case CHAR_DEAD:
                if (dead) tap_begin(KC_SPACE, TAP_CODE_DELAY, CHAR_DEAD_UP);
                else phase = READ;
                break;
            case CHAR_DEAD_UP: edge(KC_SPACE, false, true); phase = READ; wait_for(DYNAMIC_KEYMAP_MACRO_DELAY); break;
        }
    }
}
