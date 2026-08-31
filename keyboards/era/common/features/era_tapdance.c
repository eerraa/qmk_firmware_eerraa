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
#include "../storage/era_eeprom_storage.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

#define ERA_TAP_DANCE_SIGNATURE       0x4E414454UL
#define ERA_TAP_DANCE_VERSION         1U
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

typedef struct {
    uint16_t actions[ERA_TAP_DANCE_ACTION_COUNT];
    uint16_t term_ms;
} era_tapdance_slot_state_t;

typedef struct {
    uint16_t active_keycode;
    bool     active_is_tap;
} era_tapdance_runtime_t;

typedef struct {
    uint16_t on_tap;
    uint16_t on_hold;
    uint16_t on_double_tap;
    uint16_t on_tap_hold;
} era_tapdance_entry_t;

static era_tapdance_storage_t    tapdance_storage;
static era_tapdance_slot_state_t tapdance_state[ERA_TAP_DANCE_SLOT_COUNT];
static era_tapdance_runtime_t    tapdance_runtime[ERA_TAP_DANCE_SLOT_COUNT];
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

_Static_assert(sizeof(era_tapdance_slot_storage_t) == 10, "ERA Tap Dance slot storage size changed.");
_Static_assert(sizeof(era_tapdance_storage_t) == ERA_EEPROM_TAP_DANCE_CONFIG_SIZE, "ERA Tap Dance storage size changed.");

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
    era_eeprom_update_config(&tapdance_storage, ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage));
}

void era_tapdance_init(void) {
    memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
    if (era_eeprom_read_config(&tapdance_storage, ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage)) != sizeof(tapdance_storage) || !era_tapdance_storage_is_valid()) {
        era_tapdance_apply_defaults();
        era_tapdance_save_config();
    }
    era_tapdance_sync_state_from_storage();
}

void era_tapdance_reload_from_eeprom(void) {
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

static void era_tapdance_note_runtime_change(void) {
#ifdef VIA_ENABLE
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage));
#endif
}

bool era_tapdance_set_action(uint8_t slot_index, uint8_t action_index, uint16_t keycode) {
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT || action_index >= ERA_TAP_DANCE_ACTION_COUNT) {
        return false;
    }
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

static void era_tapdance_register_keycode(uint16_t keycode, bool is_tap) {
    if (!era_tapdance_keycode_is_valid(keycode)) {
        return;
    }

    keyrecord_t record = {0};
    record.event.pressed = true;
    record.event.time    = timer_read();
#ifndef NO_ACTION_TAPPING
    record.tap.count = is_tap ? 1 : 0;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    record.keycode = keycode;
#endif
    process_action(&record, action_for_keycode(keycode));
}

static void era_tapdance_unregister_keycode(uint16_t keycode, bool is_tap) {
    if (!era_tapdance_keycode_is_valid(keycode)) {
        return;
    }

    keyrecord_t record = {0};
    record.event.pressed = false;
    record.event.time    = timer_read();
#ifndef NO_ACTION_TAPPING
    record.tap.count = is_tap ? 1 : 0;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    record.keycode = keycode;
#endif
    process_action(&record, action_for_keycode(keycode));
}

static void era_tapdance_tap_keycode(uint16_t keycode, bool is_tap) {
    era_tapdance_register_keycode(keycode, is_tap);
    uint16_t delay = keycode == KC_CAPS_LOCK ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY;
    wait_ms(delay);
    era_tapdance_unregister_keycode(keycode, is_tap);
}

static void era_tapdance_set_runtime(uint8_t slot_index, uint16_t keycode, bool is_tap) {
    if (slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return;
    }
    tapdance_runtime[slot_index].active_keycode = keycode;
    tapdance_runtime[slot_index].active_is_tap  = is_tap;
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

void era_tapdance_on_each_tap(tap_dance_state_t *state, void *user_data) {
    era_tapdance_user_data_t *user = user_data;
    era_tapdance_entry_t          entry = {0};
    if (!user || user->slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return;
    }
    era_tapdance_load_entry(user->slot_index, &entry);
    if (!era_tapdance_keycode_is_valid(entry.on_tap)) {
        return;
    }
    if (state->count == 3) {
        era_tapdance_tap_keycode(entry.on_tap, true);
        era_tapdance_tap_keycode(entry.on_tap, true);
        era_tapdance_tap_keycode(entry.on_tap, true);
    } else if (state->count > 3) {
        era_tapdance_tap_keycode(entry.on_tap, true);
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

    tapdance_runtime[slot_index].active_keycode = KC_NO;
    tapdance_runtime[slot_index].active_is_tap  = false;

    switch (era_tapdance_step(state)) {
        case ERA_TD_SINGLE_TAP:
            if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_register_keycode(entry.on_tap, true);
                era_tapdance_set_runtime(slot_index, entry.on_tap, true);
            }
            break;
        case ERA_TD_SINGLE_HOLD:
            if (era_tapdance_keycode_is_valid(entry.on_hold)) {
                era_tapdance_register_keycode(entry.on_hold, false);
                era_tapdance_set_runtime(slot_index, entry.on_hold, false);
            } else if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_register_keycode(entry.on_tap, false);
                era_tapdance_set_runtime(slot_index, entry.on_tap, false);
            }
            break;
        case ERA_TD_DOUBLE_TAP:
            if (era_tapdance_keycode_is_valid(entry.on_double_tap)) {
                era_tapdance_register_keycode(entry.on_double_tap, true);
                era_tapdance_set_runtime(slot_index, entry.on_double_tap, true);
            } else if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_tap_keycode(entry.on_tap, true);
                era_tapdance_register_keycode(entry.on_tap, true);
                era_tapdance_set_runtime(slot_index, entry.on_tap, true);
            }
            break;
        case ERA_TD_DOUBLE_HOLD:
            if (era_tapdance_keycode_is_valid(entry.on_tap_hold)) {
                era_tapdance_register_keycode(entry.on_tap_hold, false);
                era_tapdance_set_runtime(slot_index, entry.on_tap_hold, false);
            } else {
                if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                    era_tapdance_tap_keycode(entry.on_tap, true);
                }
                if (era_tapdance_keycode_is_valid(entry.on_hold)) {
                    era_tapdance_register_keycode(entry.on_hold, false);
                    era_tapdance_set_runtime(slot_index, entry.on_hold, false);
                } else if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                    era_tapdance_register_keycode(entry.on_tap, false);
                    era_tapdance_set_runtime(slot_index, entry.on_tap, false);
                }
            }
            break;
        case ERA_TD_DOUBLE_SINGLE_TAP:
            if (era_tapdance_keycode_is_valid(entry.on_tap)) {
                era_tapdance_tap_keycode(entry.on_tap, true);
                era_tapdance_register_keycode(entry.on_tap, true);
                era_tapdance_set_runtime(slot_index, entry.on_tap, true);
            }
            break;
        default:
            break;
    }
}

void era_tapdance_on_reset(tap_dance_state_t *state, void *user_data) {
    (void)state;
    era_tapdance_user_data_t *user = user_data;
    if (!user || user->slot_index >= ERA_TAP_DANCE_SLOT_COUNT) {
        return;
    }

    era_tapdance_runtime_t *runtime = &tapdance_runtime[user->slot_index];
    if (era_tapdance_keycode_is_valid(runtime->active_keycode)) {
        wait_ms(TAP_CODE_DELAY);
        era_tapdance_unregister_keycode(runtime->active_keycode, runtime->active_is_tap);
    }
    runtime->active_keycode = KC_NO;
    runtime->active_is_tap  = false;
}

#endif
