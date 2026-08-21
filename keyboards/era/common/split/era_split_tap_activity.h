// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "keyboard.h"

#include "era_split_wire_protocol.h"

// FA-2's split runtime unit: the cross-half tap-hold family's core0 state.
// S1 contributes the speculative layer/mod counter sink; S2 owns the judgment
// window, the local activity record, the advertised wire composition, the
// peer cache the action-tapping seam consumes, and the family's counters.
//
// Everything here is core0's. Core1 sees only the copied wire bodies the
// scheduler publishes (the standing plan and the responder snapshot), and the
// peer cache is written only from core0's apply paths -- a cache update in
// era_invariants.md's sense, never an HID event.

// S1 counter sink, called from the action-tapping seam. The event values are
// the ERA_SPECULATIVE_NOTE_* constants declared beside the caller in
// quantum/action_tapping.c: 0 = activate, 1 = revert (tap settle),
// 2 = abort (overflow clear).
void era_split_tap_activity_note_speculative(uint8_t event);

// Local key input, from the top of era_split_keyboard_process_record() before
// every early return -- the Slice 11.5 producer's recorded position, because a
// key consumed by a feature is still local input activity. Counts the event,
// stamps the last press on the shared clock (reconstructed from the record's
// own event time, so a buffered event carries its true instant), and notes
// tap-hold settles against the open window's key.
void era_split_tap_activity_note_record(bool pressed, keypos_t key, uint16_t event_time, uint16_t keycode, uint8_t tap_count);

// The judgment window, derived at the action-tapping seam. `in_flight` is the
// engine's own fact -- a pressed, unsettled tapping key -- and the unit arms a
// window only when the effective runtime options consume other-key input
// (era_tapping's bridge; all default off, so fresh defaults never open one).
// A hold settle with retro armed moves the window to a retro-pending phase
// that survives the engine going idle and closes on the key's release.
void era_split_tap_activity_engine_window(bool in_flight, keypos_t key, uint16_t event_time);

// The cross-half judgment, consulted from the in-flight region of
// process_tapping on every event and tick. True means the in-flight tap-hold
// settles as held now (peer activity advanced inside the window, ordered by
// the shared clock); the seam performs the settle. `own_release` marks the
// call where the current event is the tapping key's own release, in which
// case the peer press must not postdate the release instant -- the
// retroactively-exact ordering the timestamps exist for. A peer press inside
// a retro-armed window cancels the pending retro tap as a side effect.
bool era_split_tap_activity_judge_hold(bool own_release, uint16_t own_release_event_time);

// The peer's activity value, from core0's two apply paths -- the standing state
// and the responder result, both DUAL-HOST only, since the section is
// closed-surface in HOST-PEER in both directions. Idempotent.
void era_split_tap_activity_apply_peer(const era_split_wire_activity_section_t *activity);

// The advertised wire body: the window flag live, the activity fields live
// only while the peer's window flag is up and frozen otherwise. Stable
// between publishes by construction, which is what the standing plan's
// publish-on-change discipline requires of every field it carries.
void era_split_tap_activity_wire_value(era_split_wire_activity_section_t *out);

// Relation rotation: drops the peer cache and returns the advertised record
// to the all-zero baseline the reopened peer's cleared cache assumes.
void era_split_tap_activity_relation_reset(void);

// Responder-confirmed activity send, from the wire's own section byte.
void era_split_tap_activity_note_sent(void);

typedef struct {
    uint32_t speculative_activate_count;
    uint32_t speculative_revert_count;
    uint32_t speculative_abort_count;
    uint32_t window_open_count;
    uint32_t judged_hold_hokp_count;
    uint32_t judged_hold_ph_count;
    uint32_t retro_cancel_count;
    uint32_t advertise_change_count;
    uint32_t sent_count;
    uint32_t peer_update_count;
} era_split_tap_activity_diagnostics_t;

void era_split_tap_activity_get_diagnostics(era_split_tap_activity_diagnostics_t *out);
