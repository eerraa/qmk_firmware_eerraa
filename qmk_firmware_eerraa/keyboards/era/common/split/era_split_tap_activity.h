// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "keyboard.h"

#include "era_split_wire_protocol.h"

// The cross-half tap-hold family's Core0 judgment window, local activity,
// advertised wire record, and peer cache consumed by action tapping.
//
// Everything here is core0's. Core1 sees only the copied wire bodies the
// scheduler publishes (the standing plan and the responder snapshot), and the
// peer cache is written only from core0's apply paths -- a cache update in
// era_invariants.md's sense, never an HID event.

// Local key input, from the top of era_split_keyboard_process_record() before
// every early return -- the Slice 11.5 producer's recorded position, because a
// key consumed by a feature is still local input activity. Counts the event,
// stamps the last press on the shared clock (reconstructed from the record's
// own event time, so a buffered event carries its true instant), and notes
// tap-hold settles against the open window's key.
void era_split_tap_activity_note_record(bool pressed, keypos_t key, uint32_t event_time, uint16_t keycode, uint8_t tap_count);

// The judgment window, derived at the action-tapping seam. `in_flight` is the
// engine's own fact -- a pressed, unsettled tapping key -- and the unit arms a
// window only when the effective runtime options consume other-key input
// (era_tapping's bridge; all default off, so fresh defaults never open one).
// A hold settle with retro armed moves the window to a retro-pending phase
// that survives the engine going idle and closes on the key's release.
void era_split_tap_activity_engine_window(bool in_flight, keypos_t key, uint32_t event_time);

// The cross-half judgment, consulted from the in-flight region of
// process_tapping on every event and tick. True means the in-flight tap-hold
// settles as held now (peer activity advanced inside the window, ordered by
// the shared clock); the seam performs the settle. `own_release` marks the
// call where the current event is the tapping key's own release, in which
// case the peer press must not postdate the release instant -- the
// retroactively-exact ordering the timestamps exist for. A peer press inside
// a retro-armed window cancels the pending retro tap as a side effect.
bool era_split_tap_activity_judge_hold(bool own_release, uint32_t own_release_event_time);

// A Tap Dance hold whose executed action is a Layer-Tap or Mod-Tap never
// reaches the tapping engine, so the dance opens the retro-pending window
// itself when the hold is registered (`held`, stamped at the dance's own
// press) and closes it after its own up. A peer press inside it cancels the
// hold's retro tap exactly as it does an engine key's. An engine key in
// flight keeps its window (its press already cleared the dance's retro
// priming locally); a retro-pending window yields to the newer hold, as it
// yields to a new engine key.
void era_split_tap_activity_dance_hold(bool held, keypos_t key, uint32_t press_event_time);

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
