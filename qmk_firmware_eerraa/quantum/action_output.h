// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Compile-time registration, not a runtime allocator. A TD runtime seat maps
 * directly to its slot; macros reserve two disjoint slots after that range.
 * Import capacity only, never a producer's execution or lifecycle types. */
#ifdef ERA_TAP_DANCE_ENABLE
#    include "process_keycode/process_tap_dance_config.h"
#    define TAP_DANCE_OWNED_ACTIONS
#    define ACTION_OUTPUT_TAP_DANCE_COUNT TAP_DANCE_MAX_SIMULTANEOUS
#else
#    define ACTION_OUTPUT_TAP_DANCE_COUNT 0
#endif
#ifdef ERA_MACRO_ENABLE
#    define ACTION_OUTPUT_OWNER_MACRO_HOLD ACTION_OUTPUT_TAP_DANCE_COUNT
#    define ACTION_OUTPUT_OWNER_MACRO_TAP (ACTION_OUTPUT_TAP_DANCE_COUNT + 1)
#    define ACTION_OUTPUT_OWNERS (ACTION_OUTPUT_TAP_DANCE_COUNT + 2)
#else
#    define ACTION_OUTPUT_OWNERS ACTION_OUTPUT_TAP_DANCE_COUNT
#endif
#if defined(ERA_TAP_DANCE_ENABLE) || defined(ERA_MACRO_ENABLE)
#    define ACTION_OWNED_OUTPUTS
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t action_output_owner_t;
#define ACTION_OUTPUT_OWNER_REGULAR UINT8_MAX

#ifdef ACTION_OWNED_OUTPUTS
/* Core0 scoped selection: save the returned token and restore it on exit,
 * including nested producer callbacks. REGULAR means ordinary QMK bit state
 * (physical inputs AND unowned firmware output), not a physical-key refcount.
 * This layer owns contributions, not producer lifetimes, layers, USB sessions,
 * queues or report timing. Implementation stays with projection in action_util.c. */
action_output_owner_t action_output_set_owner(action_output_owner_t owner);
action_output_owner_t action_output_get_owner(void);

/* Release only this owner's contributions, preserving the selected scope.
 * Keyboard/modifier changes are logical; the caller publishes the keyboard
 * report. HID cleanup uses the existing mouse/EXTRA emission paths. */
void action_output_clear_owner_mods(action_output_owner_t owner);
void action_output_clear_owner_keys(action_output_owner_t owner);
void action_output_clear_owner_hid(action_output_owner_t owner);

/* Input cancellation only: retire regular and synthetic contributions, without
 * changing the selected scope. QMK clear_keys() is a separate report-only edit. */
void action_output_reset_keys(void);
void action_output_reset_hid(void);

/* Retire ordinary input while preserving scoped producers, then reconstruct
 * their report projection. The caller owns the reason (for example wake-key
 * suppression); this does not cancel any producer or change its selected scope. */
void action_output_clear_regular(void);

#define ACTION_OUTPUT_USAGE_SYSTEM 0
#define ACTION_OUTPUT_USAGE_CONSUMER 1
/* Return whether this edge may reach the single mouse/EXTRA output state.
 * Mouse release restores a surviving opposed direction before the engine
 * retires its displaced code, preserving the live gesture's acceleration. */
bool action_output_mouse_update(uint8_t code, bool pressed);
bool action_output_usage_update(uint8_t page, uint16_t usage, uint16_t current, bool pressed);
#ifdef ERA_MACRO_ENABLE
/* Preserve the existing macro-enabled single-usage restoration policy. */
uint16_t action_output_usage_remaining(uint8_t page);
#endif
#endif

#ifdef __cplusplus
}
#endif
