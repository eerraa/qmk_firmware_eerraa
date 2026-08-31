// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

#include "era_split_mode_planner.h"

typedef enum {
    ERA_SPLIT_ROUTE_NONE = 0,
    /* Core0 no longer selects this in any relation (R2). It is kept, and kept
       at this value, because core1's standing service stamps it into the
       transaction timing as the wire operation both runtime reasons execute
       as -- a capture's `rk` means what it always did. */
    ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT,
    ERA_SPLIT_ROUTE_HOST_PEER_SOURCE_PUSH,
    ERA_SPLIT_ROUTE_ATTACH_STATUS,
} era_split_route_kind_t;

typedef enum {
    ERA_SPLIT_ROUTE_REASON_NONE = 0,
    ERA_SPLIT_ROUTE_REASON_ATTACH_STATUS_REVALIDATION,
    ERA_SPLIT_ROUTE_REASON_HOST_PEER_MATRIX_SOURCE_PUSH,
    /* Two HOST-PEER core0 service causes sat here, a response poll and an
       idle liveness beat, unproduced since the standing grant replaced both:
       the poll is RUNTIME_RESPONSE_POLL on core1's own period, and the beat is
       the grant's liveness deadline, which runs from the core that cannot be
       inside a flash write. They were kept allocated so a capture predating
       that change still decoded, and were deleted on 2026-08-17 with the
       captures. **Every value below has a producer.** */
    /* A local runtime section differs from what the wire last confirmed. */
    ERA_SPLIT_ROUTE_REASON_RUNTIME_SECTION_PUSH,
    /* Open a slot so the responder can answer, which is the only way it ever
       speaks. It executes as the existing one-byte heartbeat request: a route
       reason is not a wire payload, and the responder chooses its response
       body from its own dirty state. */
    ERA_SPLIT_ROUTE_REASON_RUNTIME_RESPONSE_POLL,
    /* Both runtime reasons are executed by the core1 standing service and
       named by it in the transaction timing -- in DUAL-HOST since Slice 11.5
       and in HOST-PEER since R2. Core0 selects neither, in either relation. */
} era_split_route_reason_t;

typedef struct {
    era_split_route_kind_t   kind;
    era_split_route_reason_t reason;
} era_split_route_selection_t;

/* Two inputs, and that is the whole of what core0 still selects on a wire it
   owns: the revalidation frame that outranks everything, and this relation's
   matrix. Everything else a serviced relation sends is core1's, under a
   published grant rather than a selected route. */
typedef struct {
    era_split_mode_t mode;

    bool attach_status_due;

    bool host_peer_source_push_due;
} era_split_router_owner_input_t;

void era_split_wire_router_select_owner(const era_split_router_owner_input_t *input, era_split_route_selection_t *selection);
