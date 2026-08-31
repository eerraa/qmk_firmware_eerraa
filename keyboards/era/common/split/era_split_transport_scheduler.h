// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "era_split_transport_scheduler_diagnostics.h"

// Policy only: plans the relation and the wire role, and opens no wire.
void era_split_transport_scheduler_init(void);
// The single boot entry point that opens the wire and launches core1. Returns
// whether the wire came up.
bool era_split_transport_scheduler_start_communication_core(void);
/* Runtime divider change: owner down, set_speed, serial recover. The
   listener's step and the agreed link raise both use it; the raise keeps
   the relation identity so the standing surface is not thrown away. */
bool era_split_transport_scheduler_apply_link_level(uint8_t level);
/* Publish-relative freshness for either Core1 request slot. It is the current
 * wire-scale standing response window plus the bounded handoff/TX margin; the
 * publisher, not the request builder, applies it to the hardware clock. */
uint32_t era_split_transport_scheduler_core1_request_queue_window_us(void);
// Returns true when this task reduces a changed local authority snapshot.
bool era_split_transport_scheduler_task(void);
// The scan path's transport step - one body for every half; role lives in the
// relation-reading paths inside it, never in the caller.
void era_split_transport_scheduler_transport_step(void);
void era_split_transport_scheduler_mark_host_peer_rgb_state_due(void);
/* Which owner this half's lighting sleep has right now, named as the question
   rather than as a mode value. The relation decides it: a HOST-PEER PEER has
   no USB session of its own -- the pair shares the HOST's -- so the sleep fact
   arrives over the wire and this half's own session may not write the render
   gate. Every other relation, including the HOST-PEER HOST, owns it locally.
   The rule is canonical in era_authority_contract.md. */
bool era_split_transport_scheduler_lighting_sleep_owner_is_wire(void);
/* The local layer edge: the one runtime section that is due immediately and
   the whole runtime producer since Slice 11.5. layer_state changes in
   keyboard_task(), not on the scan path, so its producer is the layer hook
   rather than a transport hook -- and layer_state is still the old value when
   the hook runs, which is why this marks the route rather than capturing a
   value. It only ORs a route-due bit, per the latch discipline in
   era_route_contract.md.

   The key-edge producer that stood beside it is gone with the activity window:
   a relation that polls unconditionally has nothing for a key press to open. */
void era_split_transport_scheduler_note_local_layer_change(void);
/* FA-2 S2's producer, the layer producer's twin: an advertised-activity change
   is due immediately and carries no cadence. Unlike Slice 11's per-key
   producer this one is window-bounded -- the advertised value moves only
   while a judgment window is open on one half or the other -- so fresh
   defaults never fire it and no cadence machinery hangs off it. */
void era_split_transport_scheduler_note_local_activity_change(void);
/* Slice 14's push producer: a local key edge changes the pressed baseline
   the DUAL-HOST visual cell carries. Marks only where the visual push is
   armed (DUAL-HOST Left with the RGB policy bit set); everywhere else it is
   a cached mode compare. */
void era_split_transport_scheduler_note_local_visual_change(void);
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
bool era_split_transport_scheduler_rotate_storage_relation(void);
void era_split_transport_scheduler_force_storage_recovery(bool revalidate_session);
#endif

void era_split_transport_scheduler_get_diagnostics_snapshot(era_split_transport_scheduler_diagnostics_snapshot_t *snapshot);
#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE
/* Free-running runtime section counts, read by the DUAL-HOST era block. An
   accessor rather than a reach into the scheduler's private header, for the
   same reason storage exposes a snapshot getter: the era block is a diagnostics
   unit and must not carry the scheduler's internal state layout. */
void era_split_transport_scheduler_get_dual_runtime_counts(uint32_t *tx_count, uint32_t *rx_count);

/* Index order is the order the housekeeping task body evaluates them, so a
   capture reads top to bottom against the source. Public rather than internal
   because the accessor below is, and its caller needs the array length. */
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_STORAGE 0
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_RESPONDER_DRAIN 1
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_STANDING_STATE 2
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_CORE1_INITIATOR 3
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_TIME_TOKENS 4
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_MODE 5
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_ROUTE_DUE 6
#define ERA_SPLIT_SCHEDULER_MAINT_SOURCE_COUNT 7

/* Which of the housekeeping task's contributors asked for the work `hkwork`
   counts. Read cold at print time and deliberately not in the fixed diagnostic
   record: it answers one design question -- whether core0's periodic wake
   belongs to the SESSION_STATUS lane or to the 5 ms authority-poll deadline
   that gates the task body -- and the record does not grow for it.
   `counts` receives ERA_SPLIT_SCHEDULER_MAINT_SOURCE_COUNT entries. */
void era_split_transport_scheduler_get_maintenance_source_counts(uint32_t *entry_count, uint32_t *counts);
uint32_t era_split_transport_scheduler_get_responder_snapshot_retry_count(void);
#endif
void era_split_transport_scheduler_reset_diagnostics_era_baselines(void);
bool era_split_transport_scheduler_flush_communication_core_for_diagnostics(void);
bool era_split_transport_scheduler_publish_communication_core_responder_snapshot(void);
bool era_split_transport_scheduler_drain_communication_core_responder_results(void);
