# ERA HOST-PEER Matrix Contract

Genre: contract
Canonical for: HOST-PEER latest-state matrix source semantics, the Core0/Core1
handoff boundary, HOST cache/projection ownership, relation invalidation, and
the accepted coalescing class

Route grants, cadence, standing-exchange preemption, and source-push response
carrier rules are owned by `era_route_contract.md`. MATRIX payload shape,
packing width, and section eligibility are owned by `era_wire_contract.md`.
`matrix_ready` and HOST admission are owned by `era_authority_contract.md`.
Core1 lifetime/ownership and the rule that transport does not produce HID are
owned by `era_invariants.md`. Source locations are indexed by
`era_source_map.md`.

`split/era_host_peer_source_snapshot.c` is a render-state producer for visual
baseline/RGB. It is not this matrix source and may not write key-input state.

## Source, cache, and projection

This lane is a latest-state snapshot lane, not an input-event log.

| State | Owner | Required boundary |
| --- | --- | --- |
| PEER source snapshot | `system/era_rp2040_matrix_core.c` — `era_matrix_engine_publish_local_snapshot_if_needed()` | Publish only a copied debounced local matrix snapshot. A snapshot that has not been acknowledged remains due; a later local change may replace it with the newer snapshot. |
| Core0 publication | `split/era_host_peer_matrix_link.c` and `split/scheduler/era_split_transport_scheduler_routes.c` | Capture an immutable request before Core1 owns it. The request record and its fields are canonical in `split/communication_core/era_split_communication_core_initiator.h`. |
| Core1 initiator execution | `split/communication_core/era_split_communication_core_host_peer_lanes.c` | Core1 may encode/transact only from the immutable request. It must not read the live matrix engine or live scan rows. |
| HOST responder cache | `split/communication_core/era_split_communication_core_responder_service.c`, then `system/era_rp2040_matrix_core.c` | Admit only under the authority/storage rules, copy the received matrix into the dedicated result before ACK, publish that result after the send, and let Core0 consume only a current-generation result. |
| HOST projection | `system/era_rp2040_matrix_core.c` — `era_matrix_engine_sync_peer_projection()` | Only the matrix engine may copy a valid HOST cache into peer/composed rows. Invalid cache or leaving HOST mode clears the projection. Transport never injects HID. |

Exact sequence fields, cache flags, ring sizes, record members, and counter names
are implementation facts at those source owners. They are not a second contract
surface here.

A failed or lost source-push must not retire the pending latest snapshot. A
successful result may retire only the immutable snapshot represented by that
request; if a newer snapshot appeared while it was in flight, the newer state
remains due.

> **REFUSED:** Core1 reading live matrix state or building a source-push from
> scan-owned rows.
> **WHY:** Core0 must hand off an immutable snapshot; otherwise the next scan can
> mutate the input while Core1 is encoding or transmitting it.
> **REOPENS:** a handoff whose lifetime makes the source immutable for the whole
> Core1 ownership interval.

## Response and packing boundaries

MATRIX source-push uses the carrier and section policy in
`era_wire_contract.md` and `era_route_contract.md`. The response is a bare
control ACK with no response sections. The Core1 lane diagnostics must continue
to make any non-empty response summary observable; the exact diagnostic fields
are source-owned by
`split/communication_core/era_split_communication_core_host_peer_lanes.c`.

Bit packing, reserved-bit rejection, and the matrix body width are implemented
by `split/era_split_matrix_frame.c` and specified by `era_wire_contract.md`.
This contract adds no second field list.

## Relation invalidation

A HOST-PEER relation flush must invalidate stale peer cache state so the next
projection sync removes stale projected rows, and it must make the current local
source eligible for re-publication when one exists. It must not turn relation
recovery into an event replay or erase the current local matrix source merely
because the peer relation changed.

The implementation owner is
`era_matrix_engine_flush_host_peer_relation()` in
`system/era_rp2040_matrix_core.c`; planner/storage recovery call sites are in
`split/era_split_transport_scheduler.c`. Exact reset fields and sequence
values remain source-owned.

## Accepted coalescing class

A press and release that both occur between two successful deliveries may net
to no matrix state and therefore may be invisible to the HOST. This is an
accepted consequence of latest-state transport, not a delivery defect.

> **REFUSED:** a guarantee that every press/release edge reaches the HOST on
> this lane.
> **WHY:** the contract carries current matrix state, not an event history.
> **REOPENS:** an explicit event-log carrier with its own ordering and recovery
> contract.

The same time-window can make a visual baseline show no transient light, but
visual state is a separate render surface and does not modify this matrix
contract.

## Diagnostics

Diagnostics may establish accepted pushes, ACKs, cache updates, projection
copies, and transaction outcomes, but they cannot prove how many PEER-side
changes were coalesced before delivery. Counter definitions live at
`split/era_host_peer_matrix_link.c` and the matrix engine; interpretation of
captured wire diagnostics lives in `era_capture_reading.md`.
