# ERA HOST-PEER Matrix Contract

Genre: contract
Canonical for: HOST-PEER source-push matrix semantics, snapshot/seq, HOST-side cache projection, this lane's core0/core1 handoff, and the accepted coalescing class

Route grants, due bits, and standing-exchange preemption:
`era_route_contract.md`. Payload ids, MATRIX body width, and
`ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_CORE0_LANE_SECTIONS`:
`era_wire_contract.md`. `matrix_ready` and HOST matrix admission:
`era_authority_contract.md`. `CORE1_FULL` and the HID-producer invariant:
`era_invariants.md`.

`split/era_host_peer_source_snapshot.c` stages visual baseline and RGB from
live `local_rows` / `rgb_matrix_config`. It is not the HOST-PEER matrix
snapshot. Render-state vs key input: `era_invariants.md` **render-state
clause**.

## Local source and seq

The engine in `system/era_rp2040_matrix_core.c` owns the HOST-PEER source
snapshot. One scan pass fetches one raw frame through
`era_rp2040_matrix_update_raw_rows()` (`system/era_rp2040_matrix_pio.c`),
debounces into `local_rows`, and copies those rows onto this half of
`composed_rows` when they change. The published source snapshot is
`host_peer_local_matrix`, not live `local_rows`.

`era_matrix_engine_publish_local_snapshot_if_needed()` in
`system/era_rp2040_matrix_core.c` runs from
`era_split_transport_scheduler_publish_local_matrix_if_needed()` in
`split/era_split_transport_scheduler.c` only while the authority snapshot is
`ERA_AUTH_USB_NO_HOST` and the mode is HOST-PEER PEER, or `LOCAL_NO_LINK`
until the first ready snapshot. Already-ready and `local_changed` false this
scan: skip. Otherwise a first ready, or rows that differ from
`host_peer_local_matrix`, copy the rows, set ready, set
`host_peer_local_source_push_forced`, and bump `host_peer_local_current_seq8`
through `era_matrix_engine_next_seq8()` (1..255, never 0). First ready also
marks `ERA_SPLIT_SCHEDULER_DIRTY_MATRIX_READY` and `SESSION_STATUS`
revalidation in that scheduler file. Wire `matrix_ready`:
`era_authority_contract.md` **Matrix Ready**.

`era_matrix_engine_source_push_due()` in `system/era_rp2040_matrix_core.c` is
ready AND (`forced` OR `current_seq8 != host_known_seq8`).
`era_matrix_engine_copy_source_push_rows()` copies `host_peer_local_matrix`
and `current_seq8`. A lost request leaves `host_known_seq8` unchanged; the
next capture sends the latest snapshot, not a replay of the lost one.

> **REFUSED:** an event log, or a guarantee that every press/release edge
> reaches the HOST.
> **WHY:** the lane carries the latest snapshot; a press that releases before
> the next delivery has no remaining state to send.
> **REOPENS:** an event log on this lane, with its own seq and recovery.

## Packing

`era_split_wire_pack_matrix()` / `era_split_wire_unpack_matrix()` in
`split/era_split_matrix_frame.c` map `row * MATRIX_COLS + col` onto packed
bytes. Unused high bits of the last byte are reserved-zero: pack clears them;
unpack refuses a set reserved bit. Width:
`era_wire_contract.md`.

`era_host_peer_matrix_link.c` is the adapter: it packs/unpacks and calls the
engine. It holds TX/ACK counters and the optional key-path span histogram, not
matrix rows.

## PEER handoff

Core0 captures at enqueue:
`era_host_peer_matrix_link_capture_source_push()` in
`split/era_host_peer_matrix_link.c`, from
`era_split_transport_scheduler_try_enqueue_host_peer_source_push()` in
`split/scheduler/era_split_transport_scheduler_routes.c`. The immutable
request carries packed rows, `matrix_seq`, and the core0-lane section set
(MATRIX only; `era_wire_contract.md`). Expected kinds and route selection:
`era_route_contract.md`, `era_wire_contract.md`.

> **REFUSED:** core1 reads the matrix engine or encodes source-push from live
> rows.
> **WHY:** the request must be an immutable copied snapshot; a live encode
> races the next scan.
> **REOPENS:** a snapshot the scan cannot mutate while core1 holds it.

`era_split_communication_core_process_initiator()` in
`split/communication_core/era_split_communication_core_host_peer_lanes.c`
validates that section set by exact equality, copies the packed bytes into
the frame, owns wire control/tx_seq, runs TX plus response RX, and returns
`matrix_seq` echoed from the request. Decode still walks the answer; core0
apply does not. Arrival counters on this lane (`hsrsp`, `secor`, `visn`,
`rgbn`, `newsn`) must read zero: the answer is the bare control ACK
(`era_route_contract.md` **One carrier for the response section set**).

> **REFUSED:** delete that empty response-summary return as dead code.
> **WHY:** those counters are the only measurement that the suppression is in
> force; a non-zero one falsifies it.
> **REOPENS:** a different measurement that still falsifies a non-empty
> summary.

`era_split_transport_scheduler_apply_core1_host_peer_result()` in
`split/scheduler/era_split_transport_scheduler_routes.c` runs only when
`era_split_transport_scheduler_core1_result_matches()` agrees on owner epoch,
relation generation, request generation, and lane. On
`ERA_SPLIT_TRANSACTION_RESULT_OK` it calls
`era_host_peer_matrix_link_note_source_push_accepted()`, which writes
`host_known_seq8 = matrix_seq` and clears `forced` in
`system/era_rp2040_matrix_core.c`. A newer snapshot published while that
request was in flight keeps `current_seq8 != host_known_seq8` and stays due.

## HOST cache and projection

`era_split_communication_core_responder_service_once()` in
`split/communication_core/era_split_communication_core_responder_service.c`
treats the MATRIX section — not the op id — as the claim on the dedicated
source-push result ring
(`ERA_SPLIT_COMMUNICATION_CORE_RESPONDER_SOURCE_RESULT_SLOTS` 2 in
`split/communication_core/era_split_communication_core_responder_internal.h`).
Admission is `host_matrix_admitted` AND a free source slot
(`era_authority_contract.md` **HOST-PEER Matrix Admission**; exclusivity:
`era_route_contract.md`). No slot: no ACK. Session, heartbeat, and
runtime-push results use the general ring.

The packed matrix is copied into the result record before the send. A core0
lane request answers with a one-byte control ACK (`section_mask` 0) in
`era_split_communication_core_prepare_responder_response_payload()` in that
same service file. The ring slot is published after the send. Core0 drains
only a generation-matched result:
`era_split_communication_core_responder_result_matches_current()` in
`split/communication_core/era_split_communication_core_responder.c` (owner
epoch, relation generation, snapshot generation). Then
`era_host_peer_matrix_link_accept_source_push_packed()` unpacks and
`era_matrix_engine_accept_peer_snapshot()` in
`system/era_rp2040_matrix_core.c` overwrites `host_peer_peer_matrix`, sets
cache valid, bumps `host_peer_peer_matrix_seq8` (1..255, never 0), and marks
dirty. PEER `current_seq8` does not cross in the MATRIX body; the HOST seq is
this half's own.

`era_matrix_engine_sync_peer_projection()` in `system/era_rp2040_matrix_core.c`
owns apply and clear. The scheduler supplies only
`mode == ERA_SPLIT_MODE_HOST_PEER_HOST` from
`era_split_transport_scheduler_sync_peer_matrix_projection()` in
`split/era_split_transport_scheduler.c`. HOST and cache valid/dirty/seq
changed: copy from cache and `era_matrix_engine_apply_peer_rows()` into
`peer_rows` and the other half of `composed_rows`. HOST and cache invalid:
clear projected peer rows. Not HOST: clear if a projection was valid. QMK
reads `composed_rows` through `matrix_get_row()` in
`system/era_rp2040_matrix_core.c`. Transport does not inject HID
(`era_invariants.md`).

## Flush

`era_matrix_engine_flush_host_peer_relation()` in
`system/era_rp2040_matrix_core.c` invalidates the peer cache, sets dirty,
zeros `peer_matrix_seq8` and `host_known_seq8`, and sets `forced` to current
ready. It does not clear `current_seq8` or `host_peer_local_matrix`. Callers
in `split/era_split_transport_scheduler.c`: planner
`peer_matrix_flush_required` (mode change, local authority change, peer
generation change, or secondary stale in `split/era_split_mode_planner.c`),
and `era_split_transport_scheduler_force_storage_recovery()`. Storage
close/abort is the latter's reason (`era_host_peer_storage_contract.md`,
`era_route_contract.md`).

## Accepted coalescing class

A press and its release inside one delivery interval net to no change of
state and are never observable to the HOST. That is a property of a
latest-state snapshot, not a defect. The same class holds for a visual
baseline packed from current local rows
(`split/era_host_peer_source_snapshot.c`): such a pair lights nothing on the
peer. Visual is render state and writes no matrix (`era_invariants.md`
**render-state clause**).

## Diagnostics

`era_host_peer_matrix_link_get_diagnostics_snapshot()` in
`split/era_host_peer_matrix_link.c` joins engine seq/cache counters with
link TX/ACK counts. `peer_cache_project_count` counts cache copies used for
engine apply, not scans and not wire frames. HOST `source_push_rx` and cache
update count accepted snapshots; they cannot prove how many PEER-side changes
were never sent. Lane `ok`/`miss`/`bad`/`fail` are transaction results in
`split/communication_core/era_split_communication_core_host_peer_lanes.c`.
How to read `wire csp`, including `csp` versus `source_push_tx` /
`source_push_ack` / seq: `era_capture_reading.md`.
