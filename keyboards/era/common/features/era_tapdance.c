// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "era_tapdance.h"

#if defined(ERA_TAP_DANCE_ENABLE)

#include <string.h>
#include "action.h"
#include "process_keycode/process_tap_dance.h"
#include "timer.h"
#include "wait.h"
#include "eeprom.h"
#include "../storage/era_eeprom_storage.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

#define ERA_TAP_DANCE_SIGNATURE       0x4E414454UL
#define ERA_TAP_DANCE_VERSION         1U
#define ERA_TAP_DANCE_MODE_TAG        0xD2U
#define ERA_TAP_DANCE_TERM_MIN_MS        100U
#define ERA_TAP_DANCE_TERM_MAX_MS        500U
#define ERA_TAP_DANCE_TERM_EXACT_MIN_MS  1U
#define ERA_TAP_DANCE_TERM_STEP_MS    20U
#define ERA_TAP_DANCE_TERM_DEFAULT_MS 200U

enum {
    ERA_TD_SINGLE_TAP = 1,
    ERA_TD_SINGLE_HOLD,
    ERA_TD_DOUBLE_TAP,
    ERA_TD_DOUBLE_HOLD,
    ERA_TD_DOUBLE_SINGLE_TAP,
    ERA_TD_MORE_TAPS
};

typedef struct __attribute__((packed)) {
    uint16_t actions[ERA_TAP_DANCE_ACTION_COUNT];
    uint16_t term_ms;
} era_tapdance_slot_storage_t;

typedef struct __attribute__((packed)) {
    era_tapdance_slot_storage_t slots[ERA_TAP_DANCE_SLOT_COUNT];
    uint8_t                       version;
    uint8_t                       reserved[3];
    uint32_t                      signature;
} era_tapdance_storage_t;

typedef struct __attribute__((packed)) {
    uint16_t hold_ms[ERA_TAP_DANCE_SLOT_COUNT];
    uint8_t hold_on_other;
    uint8_t version;
    uint16_t reserved;
    uint32_t signature;
} era_tapdance_timing_t;

#define ERA_TAP_DANCE_TIMING_SIGNATURE 0x4D544454UL
static era_tapdance_timing_t tapdance_timing;
_Static_assert(sizeof(era_tapdance_timing_t) == ERA_EEPROM_TAP_DANCE_TIMING_SIZE, "Tap Dance timing size changed.");

typedef struct {
    uint16_t actions[ERA_TAP_DANCE_ACTION_COUNT];
    uint16_t term_ms;
} era_tapdance_slot_state_t;

typedef struct {
    uint16_t active_keycode;
    action_t active_qmk_action;
    bool     active_is_tap;
    uint8_t  first_tap_mods;
    uint8_t  mode;
    bool     direct;
    uint16_t term_ms;
    uint16_t hold_ms;
    bool     hold_on_other;
    uint16_t actions[ERA_TAP_DANCE_ACTION_COUNT];
} era_tapdance_runtime_t;

typedef struct {
    uint16_t on_tap;
    uint16_t on_hold;
    uint16_t on_double_tap;
    uint16_t on_tap_hold;
} era_tapdance_entry_t;

static era_tapdance_storage_t    tapdance_storage;
static era_tapdance_slot_state_t tapdance_state[ERA_TAP_DANCE_SLOT_COUNT];
static era_tapdance_runtime_t    tapdance_runtime[TAP_DANCE_MAX_SIMULTANEOUS];
era_tapdance_user_data_t era_tapdance_user_data[ERA_TAP_DANCE_SLOT_COUNT] = {
    {.slot_index = 0},
    {.slot_index = 1},
    {.slot_index = 2},
    {.slot_index = 3},
    {.slot_index = 4},
    {.slot_index = 5},
    {.slot_index = 6},
    {.slot_index = 7},
};

static bool era_tapdance_load_timing(void) {
    if (era_eeprom_read_config(&tapdance_timing, ERA_EEPROM_TAP_DANCE_TIMING_OFFSET, sizeof(tapdance_timing)) == sizeof(tapdance_timing) &&
        tapdance_timing.signature == ERA_TAP_DANCE_TIMING_SIGNATURE && tapdance_timing.version == 1 && tapdance_timing.reserved == 0) return true;
    memset(&tapdance_timing, 0, sizeof(tapdance_timing));
    tapdance_timing.version = 1;
    tapdance_timing.signature = ERA_TAP_DANCE_TIMING_SIGNATURE;
    return false;
}

uint16_t era_tapdance_get_hold_term(uint8_t slot) {
    return slot < ERA_TAP_DANCE_SLOT_COUNT ? tapdance_timing.hold_ms[slot] : 0;
}

bool era_tapdance_get_hold_other(uint8_t slot) {
    return slot < ERA_TAP_DANCE_SLOT_COUNT && ((tapdance_timing.hold_on_other >> slot) & 1U);
}

static void era_tapdance_publish_timing(void) {
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_TAP_DANCE_TIMING_OFFSET, sizeof(tapdance_timing));
#endif
}

bool era_tapdance_set_hold_term(uint8_t slot, uint16_t term) {
    if (slot >= ERA_TAP_DANCE_SLOT_COUNT) return false;
    if (tapdance_timing.hold_ms[slot] != term) {
        tapdance_timing.hold_ms[slot] = term;
        era_tapdance_publish_timing();
    }
    return true;
}

bool era_tapdance_set_hold_other(uint8_t slot, uint8_t enabled) {
    if (slot >= ERA_TAP_DANCE_SLOT_COUNT || enabled > 1U) return false;
    uint8_t mask = 1U << slot;
    uint8_t next = enabled ? tapdance_timing.hold_on_other | mask : tapdance_timing.hold_on_other & ~mask;
    if (next != tapdance_timing.hold_on_other) {
        tapdance_timing.hold_on_other = next;
        era_tapdance_publish_timing();
    }
    return true;
}

static bool era_tapdance_keycode_is_valid(uint16_t keycode);
static void era_tapdance_load_entry(uint8_t slot_index, era_tapdance_entry_t *entry);
#ifdef ERA_SPLIT_TAP_ACTIVITY_ENABLE
/* split/era_split_tap_activity.h; declared rather than included, as the
 * core's split seams are. */
void era_split_tap_activity_dance_hold(bool held, keypos_t key, uint32_t press_event_time);
#endif

_Static_assert(sizeof(era_tapdance_slot_storage_t) == 10, "ERA Tap Dance slot storage size changed.");
_Static_assert(sizeof(era_tapdance_storage_t) == ERA_EEPROM_TAP_DANCE_CONFIG_SIZE, "ERA Tap Dance storage size changed.");

uint8_t era_tapdance_get_mode(uint8_t slot) {
    if (slot >= ERA_TAP_DANCE_SLOT_COUNT || tapdance_storage.reserved[1] != ERA_TAP_DANCE_MODE_TAG) return 0;
    return (tapdance_storage.reserved[slot < 4U ? 0 : 2] >> ((slot % 4U) * 2U)) & 3U;
}

static bool era_tapdance_has_override(const era_tapdance_runtime_t *runtime, uint8_t role) {
    return runtime->actions[role] != KC_TRANSPARENT;
}

static uint16_t era_tapdance_normalize_term(uint16_t term_ms) {
    term_ms = MIN(MAX(term_ms, ERA_TAP_DANCE_TERM_MIN_MS), ERA_TAP_DANCE_TERM_MAX_MS);
    return ERA_TAP_DANCE_TERM_MIN_MS + (((term_ms - ERA_TAP_DANCE_TERM_MIN_MS) / ERA_TAP_DANCE_TERM_STEP_MS) * ERA_TAP_DANCE_TERM_STEP_MS);
}

static bool era_tapdance_keycode_is_valid(uint16_t keycode) {
    return keycode != KC_NO && keycode != KC_TRANSPARENT;
}

static bool era_tapdance_storage_is_valid(void) {
    if (tapdance_storage.signature != ERA_TAP_DANCE_SIGNATURE || tapdance_storage.version != ERA_TAP_DANCE_VERSION) {
        return false;
    }
    for (uint8_t i = 0; i < ERA_TAP_DANCE_SLOT_COUNT; i++) {
        if (tapdance_storage.slots[i].term_ms < ERA_TAP_DANCE_TERM_EXACT_MIN_MS) {
            return false;
        }
    }
    if (tapdance_storage.reserved[1] != ERA_TAP_DANCE_MODE_TAG &&
        (tapdance_storage.reserved[0] != 0U || tapdance_storage.reserved[1] != 0U || tapdance_storage.reserved[2] != 0U)) return false;
    for (uint8_t slot = 0; slot < ERA_TAP_DANCE_SLOT_COUNT; ++slot) {
        uint8_t mode = era_tapdance_get_mode(slot);
        if (mode == 3U || (mode == 2U && tapdance_storage.slots[slot].actions[1] != KC_TRANSPARENT)) return false;
    }
    return true;
}

static void era_tapdance_apply_defaults(void) {
    memset(&tapdance_storage, 0, sizeof(tapdance_storage));
    for (uint8_t i = 0; i < ERA_TAP_DANCE_SLOT_COUNT; i++) {
        tapdance_storage.slots[i].term_ms = ERA_TAP_DANCE_TERM_DEFAULT_MS;
    }
    tapdance_storage.version   = ERA_TAP_DANCE_VERSION;
    tapdance_storage.signature = ERA_TAP_DANCE_SIGNATURE;
}

static void era_tapdance_sync_state_from_storage(void) {
    for (uint8_t i = 0; i < ERA_TAP_DANCE_SLOT_COUNT; i++) {
        for (uint8_t a = 0; a < ERA_TAP_DANCE_ACTION_COUNT; a++) {
            tapdance_state[i].actions[a] = tapdance_storage.slots[i].actions[a];
        }
        tapdance_state[i].term_ms         = tapdance_storage.slots[i].term_ms;
    }
}

void era_tapdance_save_config(void) {
    if (!eeprom_begin_transaction()) return;
    era_eeprom_update_config(&tapdance_storage, ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage));
    era_eeprom_update_config(&tapdance_timing, ERA_EEPROM_TAP_DANCE_TIMING_OFFSET, sizeof(tapdance_timing));
    eeprom_end_transaction();
}

void era_tapdance_init(void) {
    /* Retire outputs while their executed actions are still available. The
     * core retains release tombstones and invalidates older queued TD input. */
    tap_dance_cancel_all();
    memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
    bool timing_valid = era_tapdance_load_timing();
    if (era_eeprom_read_config(&tapdance_storage, ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage)) != sizeof(tapdance_storage) || !era_tapdance_storage_is_valid()) {
        era_tapdance_apply_defaults();
        timing_valid = false;
    }
    if (!timing_valid) era_tapdance_save_config();
    era_tapdance_sync_state_from_storage();
}

void era_tapdance_reload_from_eeprom(void) {
    era_tapdance_load_timing();
    if (era_eeprom_read_config(&tapdance_storage, ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage)) != sizeof(tapdance_storage) || !era_tapdance_storage_is_valid()) {
        era_tapdance_apply_defaults();
    }
    era_tapdance_sync_state_from_storage();
}

static uint16_t era_tapdance_get_term_ms(uint16_t keycode) {
    uint8_t slot_index = QK_TAP_DANCE_GET_INDEX(keycode);
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return ERA_TAP_DANCE_TERM_DEFAULT_MS;
    }
    return tapdance_state[slot_index].term_ms;
}

uint16_t tap_dance_get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    (void)record;
    return era_tapdance_get_term_ms(keycode);
}

uint16_t tap_dance_remap_keycode(uint16_t keycode) {
    /* Official VIA customKeycodes TD0–TD7 and custom-app tapdanceKeycodes both
       assign QK_KB_n at ERA_TAP_DANCE_KEYCODE_BASE. */
    if (keycode >= ERA_TAP_DANCE_KEYCODE_BASE && keycode < ERA_TAP_DANCE_KEYCODE_BASE + ERA_TAP_DANCE_SLOT_COUNT) {
        return TD(keycode - ERA_TAP_DANCE_KEYCODE_BASE);
    }
    return keycode;
}

/* **When a dance is decided.** A release decides a dance as soon as no later
 * press could change the outcome: without On Double Tap and On Tap-Hold the
 * first release is a single tap, and with On Double Tap the second release is
 * a double tap. Vial waits for Term in both cases; deciding at the release
 * removes only that delay. Every other layout keeps QMK's timeout. The hook is
 * the weak tap_dance_finish_on_release() in
 * quantum/process_keycode/process_tap_dance.c (era_qmk_fork_ledger.md); the
 * policy is this unit's. Term decides a hold on every layout: the hold
 * deadline is not touched here. */
bool tap_dance_finish_on_release(const tap_dance_action_t *action, const tap_dance_state_t *state) {
    if (!action || !state) {
        return false;
    }
    era_tapdance_user_data_t *user = action->user_data;
    if (!user || user->slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return false;
    }
    if (state->runtime_index < TAP_DANCE_MAX_SIMULTANEOUS && tapdance_runtime[state->runtime_index].mode != 0U) {
        const era_tapdance_runtime_t *runtime = &tapdance_runtime[state->runtime_index];
        return state->count == 2U || (state->count == 1U && !era_tapdance_has_override(runtime, 2) && !era_tapdance_has_override(runtime, 3));
    }
    era_tapdance_entry_t entry = {0};
    era_tapdance_load_entry(user->slot_index, &entry);
    return (state->count == 1 && !era_tapdance_keycode_is_valid(entry.on_double_tap) && !era_tapdance_keycode_is_valid(entry.on_tap_hold)) ||
           (state->count == 2 && era_tapdance_keycode_is_valid(entry.on_double_tap));
}

static void era_tapdance_note_runtime_change(void) {
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage));
#endif
}

bool era_tapdance_set_mode(uint8_t slot, uint8_t mode) {
    if (slot >= ERA_TAP_DANCE_SLOT_COUNT || mode > 2U ||
        (mode == 2U && tapdance_state[slot].actions[1] != KC_TRANSPARENT)) return false;
    if (era_tapdance_get_mode(slot) == mode) return true;
    if (tapdance_storage.reserved[1] != ERA_TAP_DANCE_MODE_TAG) memset(tapdance_storage.reserved, 0, sizeof(tapdance_storage.reserved));
    tapdance_storage.reserved[1] = ERA_TAP_DANCE_MODE_TAG;
    uint8_t *bits = &tapdance_storage.reserved[slot < 4U ? 0 : 2];
    uint8_t shift = (slot % 4U) * 2U;
    *bits = (*bits & ~(3U << shift)) | (mode << shift);
    era_tapdance_note_runtime_change();
    return true;
}

bool era_tapdance_set_action(uint8_t slot_index, uint8_t action_index, uint16_t keycode) {
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT || action_index >= ERA_TAP_DANCE_ACTION_COUNT) {
        return false;
    }
    if (action_index == 1U && era_tapdance_get_mode(slot_index) == 2U && keycode != KC_TRANSPARENT) return false;
    if (tapdance_storage.slots[slot_index].actions[action_index] == keycode) {
        return true;
    }
    tapdance_storage.slots[slot_index].actions[action_index] = keycode;
    tapdance_state[slot_index].actions[action_index]         = keycode;
    era_tapdance_note_runtime_change();
    return true;
}

bool era_tapdance_set_slot_term_ms(uint8_t slot_index, uint16_t term_ms) {
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return false;
    }
    uint16_t next = era_tapdance_normalize_term(term_ms);
    if (tapdance_storage.slots[slot_index].term_ms == next) {
        return true;
    }
    tapdance_storage.slots[slot_index].term_ms = next;
    tapdance_state[slot_index].term_ms         = next;
    era_tapdance_note_runtime_change();
    return true;
}

bool era_tapdance_set_slot_term_ms_exact(uint8_t slot_index, uint16_t term_ms) {
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return false;
    }
    if (term_ms < ERA_TAP_DANCE_TERM_EXACT_MIN_MS) {
        return false;
    }
    if (tapdance_storage.slots[slot_index].term_ms == term_ms) {
        return true;
    }
    tapdance_storage.slots[slot_index].term_ms = term_ms;
    tapdance_state[slot_index].term_ms         = term_ms;
    era_tapdance_note_runtime_change();
    return true;
}

uint16_t era_tapdance_get_action(uint8_t slot_index, uint8_t action_index) {
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT || action_index >= ERA_TAP_DANCE_ACTION_COUNT) {
        return KC_NO;
    }
    return tapdance_state[slot_index].actions[action_index];
}

uint16_t era_tapdance_get_slot_term_ms(uint8_t slot_index) {
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return ERA_TAP_DANCE_TERM_DEFAULT_MS;
    }
    return tapdance_state[slot_index].term_ms;
}

/* One edge of an executed action. The record carries the dance's own position
 * as a tick event: position-keyed QMK state such as retro tapping sees this
 * input, never whatever sits at (0,0), and no dance takes it for an input. A
 * keycode without a QMK action runs through the quantum handlers. */
static void era_tapdance_run_action(const tap_dance_state_t *state, uint16_t keycode, action_t action, bool pressed, bool is_tap) {
    keyrecord_t record = {0};
    record.event.key     = state->key;
    record.event.pressed = pressed;
    record.event.time    = timer_read32();
#ifndef NO_ACTION_TAPPING
    record.tap.count = is_tap ? 1 : 0;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    record.keycode = keycode;
#endif
    if (action.code == ACTION_NO) {
        tap_dance_run_quantum_keycode(&record, keycode);
    } else {
        process_action(&record, action);
    }
}

static void era_tapdance_register_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap) {
    if (era_tapdance_keycode_is_valid(keycode)) {
        era_tapdance_run_action(state, keycode, action_for_keycode(keycode), true, is_tap);
    }
}

static void era_tapdance_unregister_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap) {
    if (era_tapdance_keycode_is_valid(keycode)) {
        era_tapdance_run_action(state, keycode, action_for_keycode(keycode), false, is_tap);
    }
}

/* **How wide a synthesized tap is.** QMK's own rule, applied to every tap this
 * unit synthesizes so a keycode behaves the same on a TD slot as on a Layer-Tap
 * or a plain tap_code(): Caps Lock is held TAP_HOLD_CAPS_DELAY, because macOS
 * ignores a shorter Caps tap (quantum/action.c does the same for LT/MT); every
 * other keycode is held TAP_CODE_DELAY. A physical hold's release owes no
 * width, the switch already supplied it. Vial waits its runtime tap-code delay
 * for every keycode instead; that setting has no ERA surface, so a Caps tap
 * would be one report apart and macOS would drop it. The width is requested
 * through tap_code_wait() (quantum/action.c), never waited for: ERA's USB
 * transport keeps it as a report interval (era_hid_report_contract.md), so
 * the scan loop does not stall for it. */
static uint16_t era_tapdance_tap_width_ms(uint16_t keycode) {
    return keycode == KC_CAPS_LOCK ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY;
}

static void era_tapdance_tap_keycode(const tap_dance_state_t *state, uint16_t keycode, bool is_tap) {
    era_tapdance_register_keycode(state, keycode, is_tap);
    tap_code_wait(keycode, era_tapdance_tap_width_ms(keycode));
    era_tapdance_unregister_keycode(state, keycode, is_tap);
}

static void era_tapdance_set_runtime(const tap_dance_state_t *state, uint16_t keycode, bool is_tap) {
    if (state->runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS) {
        return;
    }
    tapdance_runtime[state->runtime_index].active_keycode = keycode;
    tapdance_runtime[state->runtime_index].active_qmk_action = action_for_keycode(keycode);
    tapdance_runtime[state->runtime_index].active_is_tap  = is_tap;
#ifdef ERA_SPLIT_TAP_ACTIVITY_ENABLE
    /* A peer-half press during this LT/MT hold cancels its retro tap, as it
     * does for an LT/MT the tapping engine holds. */
    if (!is_tap && is_tap_action(tapdance_runtime[state->runtime_index].active_qmk_action)) {
        era_split_tap_activity_dance_hold(true, state->key, state->last_tap_time);
    }
#endif
}

static void era_tapdance_load_entry(uint8_t slot_index, era_tapdance_entry_t *entry) {
    if (!entry || slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return;
    }
    entry->on_tap        = tapdance_state[slot_index].actions[0];
    entry->on_hold       = tapdance_state[slot_index].actions[1];
    entry->on_double_tap = tapdance_state[slot_index].actions[2];
    entry->on_tap_hold   = tapdance_state[slot_index].actions[3];
}

static uint8_t era_tapdance_step(const tap_dance_state_t *state) {
    if (!state) {
        return ERA_TD_SINGLE_TAP;
    }
    if (state->count == 1) {
        return (state->interrupted || !state->pressed) ? ERA_TD_SINGLE_TAP : ERA_TD_SINGLE_HOLD;
    }
    if (state->count == 2) {
        if (state->interrupted) {
            return ERA_TD_DOUBLE_SINGLE_TAP;
        }
        return state->pressed ? ERA_TD_DOUBLE_HOLD : ERA_TD_DOUBLE_TAP;
    }
    return ERA_TD_MORE_TAPS;
}

uint16_t tap_dance_get_decision_term(const tap_dance_state_t *state) {
    if (state->runtime_index < TAP_DANCE_MAX_SIMULTANEOUS && tapdance_runtime[state->runtime_index].mode != 0U) {
        const era_tapdance_runtime_t *runtime = &tapdance_runtime[state->runtime_index];
        return state->pressed ? runtime->hold_ms : runtime->term_ms;
    }
    return era_tapdance_get_term_ms(TD(state->index));
}

bool tap_dance_hold_on_interrupt(const tap_dance_state_t *state) {
    if (!state->pressed || state->runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS) return false;
    const era_tapdance_runtime_t *runtime = &tapdance_runtime[state->runtime_index];
    if (runtime->mode == 0U || !runtime->hold_on_other) return false;
    if (state->count == 1U) return !runtime->direct && era_tapdance_has_override(runtime, 1);
    return state->count == 2U && (era_tapdance_has_override(runtime, 3) || era_tapdance_has_override(runtime, 1));
}

void era_tapdance_on_each_release(tap_dance_state_t *state, void *user_data) {
    (void)user_data;
    if (state->runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS) return;
    era_tapdance_runtime_t *runtime = &tapdance_runtime[state->runtime_index];
    if (!runtime->direct || state->finished) return;
    if (state->count == 1U && era_tapdance_keycode_is_valid(runtime->active_keycode)) {
        era_tapdance_run_action(state, runtime->active_keycode, runtime->active_qmk_action, false, true);
        runtime->active_keycode = KC_NO;
    }
}

void era_tapdance_on_each_tap(tap_dance_state_t *state, void *user_data) {
    era_tapdance_user_data_t *user = user_data;
    era_tapdance_entry_t          entry = {0};
    if (!user || user->slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return;
    }
    if (state->count == 1 && state->runtime_index < TAP_DANCE_MAX_SIMULTANEOUS) {
        /* A Double Hold's embedded tap stands for this first press. */
        tapdance_runtime[state->runtime_index].first_tap_mods = state->weak_mods;
    }
    era_tapdance_load_entry(user->slot_index, &entry);
    if (state->runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS) return;
    era_tapdance_runtime_t *runtime = &tapdance_runtime[state->runtime_index];
    if (state->count == 1U) {
        runtime->mode = era_tapdance_get_mode(user->slot_index);
        runtime->term_ms = tapdance_state[user->slot_index].term_ms;
        runtime->hold_ms = era_tapdance_get_hold_term(user->slot_index);
        if (!runtime->hold_ms) runtime->hold_ms = runtime->term_ms;
        runtime->hold_on_other = era_tapdance_get_hold_other(user->slot_index);
        memcpy(runtime->actions, tapdance_state[user->slot_index].actions, sizeof(runtime->actions));
        runtime->direct = runtime->mode == 2U || (runtime->mode == 1U &&
            !era_tapdance_has_override(runtime, 1) && !era_tapdance_has_override(runtime, 2) && !era_tapdance_has_override(runtime, 3));
    }
    if (runtime->direct) {
        if (state->count == 1U) {
            era_tapdance_set_runtime(state, entry.on_tap, true);
            era_tapdance_register_keycode(state, entry.on_tap, true);
            if (!era_tapdance_has_override(runtime, 2) && !era_tapdance_has_override(runtime, 3)) state->finished = true;
            /* The first edge has already consumed its modifiers. */
            state->weak_mods = 0;
#ifndef NO_ACTION_ONESHOT
            state->oneshot_mods = 0;
#endif
        }
        return;
    }
    if (!era_tapdance_keycode_is_valid(entry.on_tap)) {
        return;
    }
    if (state->count == 3) {
        era_tapdance_tap_keycode(state, entry.on_tap, true);
        era_tapdance_tap_keycode(state, entry.on_tap, true);
        era_tapdance_tap_keycode(state, entry.on_tap, true);
    } else if (state->count > 3) {
        era_tapdance_tap_keycode(state, entry.on_tap, true);
    }
}

void era_tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data) {
    era_tapdance_user_data_t *user = user_data;
    era_tapdance_entry_t          entry = {0};
    if (!user || user->slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return;
    }

    uint8_t slot_index = user->slot_index;
    era_tapdance_load_entry(slot_index, &entry);

    if (state->runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS) {
        return;
    }
    era_tapdance_runtime_t *runtime = &tapdance_runtime[state->runtime_index];
    if (runtime->mode != 0U)
    {
      /* Immediate mode has already emitted the first physical press. All
       * other decisions use the captured actions, including explicit KC_NO. */
      if (runtime->direct && state->count == 1U) return;
      bool hold = state->pressed && !state->interrupted;
      uint16_t keycode = runtime->actions[0];
      bool prefix_tap = false;
      if (state->count == 1U)
      {
        if (hold && era_tapdance_has_override(runtime, 1)) keycode = runtime->actions[1];
      }
      else if (state->count == 2U)
      {
        uint8_t role = hold ? 3 : 2;
        if (!state->interrupted && era_tapdance_has_override(runtime, role)) keycode = runtime->actions[role];
        else
        {
          prefix_tap = !runtime->direct;
          if (hold && era_tapdance_has_override(runtime, 1)) keycode = runtime->actions[1];
        }
      }
      else return;
      if (prefix_tap)
      {
        uint8_t mods = runtime->first_tap_mods | state->weak_mods;
        add_weak_mods(mods);
        era_tapdance_tap_keycode(state, runtime->actions[0], true);
        del_weak_mods(mods);
        if (!hold) add_weak_mods(state->weak_mods);
        send_keyboard_report();
      }
      era_tapdance_set_runtime(state, keycode, !hold);
      era_tapdance_register_keycode(state, keycode, !hold);
      return;
    }

    tapdance_runtime[state->runtime_index].active_keycode = KC_NO;
    tapdance_runtime[state->runtime_index].active_is_tap  = false;

    switch (era_tapdance_step(state)) {
        case ERA_TD_SINGLE_TAP:
            if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_register_keycode(state, entry.on_tap, true);
                era_tapdance_set_runtime(state, entry.on_tap, true);
            }
            break;
        case ERA_TD_SINGLE_HOLD:
            if (era_tapdance_keycode_is_valid(entry.on_hold)) {
                era_tapdance_register_keycode(state, entry.on_hold, false);
                era_tapdance_set_runtime(state, entry.on_hold, false);
            } else if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_register_keycode(state, entry.on_tap, false);
                era_tapdance_set_runtime(state, entry.on_tap, false);
            }
            break;
        case ERA_TD_DOUBLE_TAP:
            if (era_tapdance_keycode_is_valid(entry.on_double_tap)) {
                era_tapdance_register_keycode(state, entry.on_double_tap, true);
                era_tapdance_set_runtime(state, entry.on_double_tap, true);
            } else if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_tap_keycode(state, entry.on_tap, true);
                era_tapdance_register_keycode(state, entry.on_tap, true);
                era_tapdance_set_runtime(state, entry.on_tap, true);
            }
            break;
        case ERA_TD_DOUBLE_HOLD:
            if (era_tapdance_keycode_is_valid(entry.on_tap_hold)) {
                era_tapdance_register_keycode(state, entry.on_tap_hold, false);
                era_tapdance_set_runtime(state, entry.on_tap_hold, false);
            } else {
                if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                    /* The embedded tap stands for the first press and, like
                     * any tap, carries the modifiers held at either press.
                     * The hold that follows keeps only live owners. */
                    const uint8_t mods = tapdance_runtime[state->runtime_index].first_tap_mods | state->weak_mods;
                    add_weak_mods(mods);
                    era_tapdance_tap_keycode(state, entry.on_tap, true);
                    del_weak_mods(mods);
                    send_keyboard_report();
                }
                if (era_tapdance_keycode_is_valid(entry.on_hold)) {
                    era_tapdance_register_keycode(state, entry.on_hold, false);
                    era_tapdance_set_runtime(state, entry.on_hold, false);
                } else if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                    era_tapdance_register_keycode(state, entry.on_tap, false);
                    era_tapdance_set_runtime(state, entry.on_tap, false);
                }
            }
            break;
        case ERA_TD_DOUBLE_SINGLE_TAP:
            if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_tap_keycode(state, entry.on_tap, true);
                era_tapdance_register_keycode(state, entry.on_tap, true);
                era_tapdance_set_runtime(state, entry.on_tap, true);
            }
            break;
        default:
            break;
    }
}

void era_tapdance_on_reset(tap_dance_state_t *state, void *user_data) {
    era_tapdance_user_data_t *user = user_data;
    if (!user || user->slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return;
    }

    if (state->runtime_index >= TAP_DANCE_MAX_SIMULTANEOUS) {
        return;
    }
    era_tapdance_runtime_t *runtime = &tapdance_runtime[state->runtime_index];
    if (era_tapdance_keycode_is_valid(runtime->active_keycode)) {
        /* A tap the decision registered is released here, one synthesized
           width later (era_tapdance_tap_width_ms); a hold is released as the
           switch is. */
        if (runtime->active_is_tap && !(runtime->direct && state->count == 1U)) {
            tap_code_wait(runtime->active_keycode, era_tapdance_tap_width_ms(runtime->active_keycode));
        }
        {
            /* Every action receives its own up. Resource contributions, not
             * an identical-action suppression, preserve the other owners. */
            era_tapdance_run_action(state, runtime->active_keycode, runtime->active_qmk_action, false, runtime->active_is_tap);
#ifdef ERA_SPLIT_TAP_ACTIVITY_ENABLE
            if (!runtime->active_is_tap) {
                era_split_tap_activity_dance_hold(false, state->key, 0);
            }
#endif
        }
    }
    runtime->active_keycode = KC_NO;
    runtime->active_is_tap  = false;
    runtime->mode = 0;
    runtime->direct = false;
}

#endif
