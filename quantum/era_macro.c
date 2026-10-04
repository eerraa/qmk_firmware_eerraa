// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
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
#    define DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE TOTAL_EEPROM_BYTE_COUNT
#endif
_Static_assert(DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE <= UINT16_MAX, "Macro snapshot index width");
/* One snapshot, never one per queued request. The AVR board does not select
 * this module. No heap and no storage access on idle or active execution. */
static uint8_t snapshot[DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE];
static uint8_t pending[ERA_MACRO_QUEUE_CAPACITY], head, count;
static uint16_t cursor, limit;
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
    outputs = true;
    if (down) register_code(code);
    else unregister_code(code);
    action_output_set_owner(previous);
}
void era_macro_cancel(void) {
    active = waiting = false;
    head = count = 0;
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
bool era_macro_request(uint8_t id) {
    era_macro_platform_sync();
    if (!valid || id >= dynamic_keymap_macro_get_count() || count == ERA_MACRO_QUEUE_CAPACITY) return false;
    pending[(head + count) % ERA_MACRO_QUEUE_CAPACITY] = id;
    ++count;
    return true;
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
    cursor = at;
    limit = size;
    active = true;
    waiting = false;
    phase = READ;
    return true;
}
static uint8_t read_byte(void) { return cursor < limit ? snapshot[cursor++] : 0; }
static void finish(bool malformed) {
    macro_clear(true);
    if (malformed) macro_clear(false);
    send_keyboard_report();
    active = waiting = false;
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
    if (!valid || paused || (!active && !count)) return;
    if (era_macro_platform_pressured()) return;
    if (waiting) {
        elapsed(timer_read32());
        if (waiting) return;
    }
    if (!active) {
        uint8_t id = pending[head];
        head = (head + 1U) % ERA_MACRO_QUEUE_CAPACITY;
        --count;
        /* At most one bounded snapshot/selection per housekeeping pass. */
        if (!start(id)) return;
    }
    for (uint8_t budget = 0; budget < 16 && active && !waiting; ++budget) {
        if (era_macro_platform_pressured()) return;
        switch (phase) {
            case READ: {
                uint8_t ascii = read_byte();
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
