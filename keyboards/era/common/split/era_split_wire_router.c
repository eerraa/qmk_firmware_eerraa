// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_split_wire_router.h"

#include <stddef.h>

static void era_split_route_clear(era_split_route_selection_t *selection) {
    if (selection == NULL) {
        return;
    }
    selection->kind   = ERA_SPLIT_ROUTE_NONE;
    selection->reason = ERA_SPLIT_ROUTE_REASON_NONE;
}

static void era_split_route_set(era_split_route_selection_t *selection, era_split_route_kind_t kind, era_split_route_reason_t reason) {
    selection->kind   = kind;
    selection->reason = reason;
}

void era_split_wire_router_select_owner(const era_split_router_owner_input_t *input, era_split_route_selection_t *selection) {
    era_split_route_clear(selection);
    if (input == NULL || selection == NULL) {
        return;
    }

    if (input->attach_status_due) {
        era_split_route_set(selection, ERA_SPLIT_ROUTE_ATTACH_STATUS, ERA_SPLIT_ROUTE_REASON_ATTACH_STATUS_REVALIDATION);
        return;
    }

    /* The matrix, and since R2 it is the whole of what this relation selects.
       It is untouched by that lane on purpose: same event route, same rings,
       same capacity slot, same priority. Key input is what this relation
       exists to carry, and the grant runs beside it rather than through it --
       core1 reaches the standing service only on a pass where the request
       queue and the storage lane both did nothing, which is already the pass
       order. */
    if (input->mode == ERA_SPLIT_MODE_HOST_PEER_PEER && input->host_peer_source_push_due) {
        era_split_route_set(selection, ERA_SPLIT_ROUTE_HOST_PEER_SOURCE_PUSH, ERA_SPLIT_ROUTE_REASON_HOST_PEER_MATRIX_SOURCE_PUSH);
        return;
    }

    /* Neither serviced relation selects a runtime route here any more, and the
       sentence is now one sentence rather than two. DUAL-HOST reached this at
       Slice 11.5; HOST-PEER reaches it at R2, when its response poll, its
       liveness heartbeat and its AUTHORITY push moved onto the same standing
       grant. A responder never selected anything in either.

       The route reasons still exist and still name what runs: core1 stamps
       ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT with RUNTIME_SECTION_PUSH or
       RUNTIME_RESPONSE_POLL into the transaction timing, so a capture reads
       the identifiers it always did. What no longer exists is a core0
       selection of them, and with it the per-exchange submit and drain -- on
       the HOST-PEER PEER that was two core0 wakes per exchange at the then
       50 Hz, paid on the half whose keys ride the wire. The figure tracked the
       poll period and the cost it names retired with the selection, so it is
       left at the value the measurement was taken at.

       Route priority is not lost with the arms. Storage exclusivity and a
       pending SESSION_STATUS clear the plan's enable bit, which is the same
       precedence expressed once instead of re-derived per poll
       (era_route_contract.md, the standing exchange grant). What the enable
       bit deliberately does not stop is liveness: core1 still holds the wire
       through a durable apply, which in HOST-PEER is the hole this lane
       exists to close. */
}
