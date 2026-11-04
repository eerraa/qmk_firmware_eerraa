// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Macro provenance a key record carries (keyrecord_t.era_macro_origin,
 * quantum/action.h). Zero, which every record built without it keeps, never
 * starts a macro. */
#define ERA_MACRO_ORIGIN_START 0x01U /* begun with no macro admitted: it may start one */
#define ERA_MACRO_ORIGIN_BLOCKED 0x02U /* ingress drops this edge before any action */
/* Admission of one macro; refused, never queued, while another is admitted.
 * An API start has no starting key, so Stop on release does not apply to it. */
bool era_macro_request(uint8_t id);
/* A macro key's press from process_record_via(), with its record's
 * provenance and press identity (keyrecord_t.era_macro_press). */
bool era_macro_request_key(uint8_t id, uint8_t row, uint8_t col, uint8_t origin, uint8_t press);
/* A genuine input edge from pre_process_record_quantum(), before any handler
 * or the tapping queue: judges the playing macro's stop options and returns
 * the record's provenance; `press` receives the identity of the physical
 * press the edge belongs to (0 outside the matrix). */
uint8_t era_macro_input(uint8_t row, uint8_t col, bool pressed, uint16_t keycode, uint8_t *press);
void era_macro_task(void);
void era_macro_cancel(void);
void era_macro_session(uint32_t generation, bool valid, bool suspended);
/* Platform updates lifecycle before execution and before transport reopen. */
void era_macro_platform_sync(void);
bool era_macro_platform_pressured(void);
bool era_macro_session_current(uint32_t generation);
#ifdef ERA_MACRO_OPTIONS_ENABLE
/* Per-macro playback options, owned by keyboards/era/common/system/era_macro_options.c.
 * Admission snapshots them; VIA's storage initializer, which clears the
 * macros, restores their defaults (the macro RESET command does not). */
uint8_t  era_macro_options_flags(uint8_t id);
uint16_t era_macro_options_hold_ms(uint8_t id);
uint8_t  era_macro_options_repeat_count(uint8_t id);
void     era_macro_options_reset_defaults(void);
#endif
#ifdef ERA_MACRO_ENABLE
#    include "action_output.h"
/* Producer policy stays here; inline it so the repeated-key path needs only
 * the shared scope read, without an extra feature-to-core call frame. */
static inline bool era_macro_emitting(void) {
    action_output_owner_t owner = action_output_get_owner();
    return owner == ACTION_OUTPUT_OWNER_MACRO_HOLD || owner == ACTION_OUTPUT_OWNER_MACRO_TAP;
}
#endif
