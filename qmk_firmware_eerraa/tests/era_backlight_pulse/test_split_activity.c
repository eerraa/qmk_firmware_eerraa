// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* The split judgment unit's wire types are C-only, so they stay on this side of
   the C++ fixture. The split transport and wire payload it calls are outside
   this target and are stubbed here. */
#include "keyboards/era/common/split/era_split_tap_activity.h"
#include "keyboards/era/common/split/era_split_transport_scheduler.h"
#include "keyboards/era/common/split/era_split_wire_payload.h"

void era_split_transport_scheduler_note_local_activity_change(void) {}

bool era_split_wire_activity_equal(const era_split_wire_activity_section_t *lhs, const era_split_wire_activity_section_t *rhs) {
    return lhs->window_open == rhs->window_open && lhs->press_count == rhs->press_count && lhs->release_count == rhs->release_count &&
           lhs->last_press_ms == rhs->last_press_ms && lhs->last_release_ms == rhs->last_release_ms;
}

/* A DUAL-HOST peer press reaches this half only as the other half's activity
   record, never as a local event. */
void era_test_split_peer_press(uint32_t press_ms) {
    era_split_wire_activity_section_t peer = {0};
    peer.press_count   = 1;
    peer.last_press_ms = press_ms;
    era_split_tap_activity_apply_peer(&peer);
}

void era_test_split_reset(void) {
    era_split_tap_activity_relation_reset();
}
