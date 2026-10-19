// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

typedef enum {
    ERA_SPLIT_SCHEDULER_DIRTY_AUTHORITY = 1U << 0,
    ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY = 1U << 1,
    ERA_SPLIT_SCHEDULER_DIRTY_PEER_SESSION = 1U << 2,
    ERA_SPLIT_SCHEDULER_DIRTY_PEER_STALE = 1U << 3,
    ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY = 1U << 4,
} era_split_scheduler_dirty_flags_t;

/* Two live bits, and four retired un-reused around them. A `due=` field is
   captured, so a retired bit stays unassigned rather than being recycled --
   the same rule the wire ids and the `SESSION_STATUS` flag bits follow, and
   for the same reason: one recycled bit makes every capture that shows it
   ambiguous between two eras.
 *
 * Bit 1 was the HOST-PEER liveness heartbeat and bit 3 its HOST-source
 * response poll; bit 6 was its AUTHORITY push. All three retired at R2, when
 * that relation's initiator moved onto the standing grant and stopped
 * selecting a runtime route at all. Bit 5 was an earlier response poll and has
 * been unassigned since. */
typedef enum {
    ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS = 1U << 0,
    ERA_SPLIT_SCHEDULER_ROUTE_DUE_HOST_PEER_SOURCE_PUSH = 1U << 2,
    /* Not a route since Slice 11.5. It is the standing plan's publication
       bit, and it lives in this word because scan_idle() reads this word: a
       layer edge must reach the wire in about one scan, and the alternative
       was a second per-scan predicate. It stays DUAL-HOST's alone after R2:
       HOST-PEER publishes a plan too, but carries no INPUT_LAYER and so has no
       field a scan could move (`era_split_transport_scheduler_timing.c`). */
    ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH = 1U << 4,
} era_split_scheduler_route_due_flags_t;

typedef enum {
    ERA_SPLIT_SCHEDULER_MAINT_DUE_AUTHORITY_SAMPLE = 1U << 0,
} era_split_scheduler_maintenance_due_flags_t;

void era_split_transport_scheduler_mark_dirty(uint8_t flags);
void era_split_transport_scheduler_mark_route_due(uint8_t flags);
void era_split_transport_scheduler_mark_maintenance_due(uint8_t flags);
