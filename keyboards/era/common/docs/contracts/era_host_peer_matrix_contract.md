# ERA HOST-PEER Matrix Contract

Genre: contract
Canonical for: HOST-PEER source-push matrix semantics, snapshot/seq, HOST-side cache projection, this lane's core0/core1 handoff, and the accepted coalescing class

## Model

- PEER owns its local matrix source, and publishes the latest debounced local
  snapshot to the matrix engine's HOST-PEER source snapshot (`system/era_rp2040_matrix_core.c`).
- PEER sends source-push only in confirmed HOST-PEER PEER mode; HOST receives
  it only in confirmed HOST-PEER HOST mode.
- The HOST-PEER wire adapter in `split/era_host_peer_matrix_link.c` packs and
  unpacks source-push matrix payloads and owns no matrix state; HOST stores
  accepted PEER snapshots in the matrix engine's peer matrix cache.
- **The engine owns the apply, not the scheduler.** It compares peer cache
  valid/seq state and applies or clears peer rows only when that state changes;
  the scheduler decides only whether HOST-PEER projection is active for the
  current relation. The engine owns the peer row buffer and the QMK-visible
  composed matrix rows, and QMK matrix/debounce/key processing produces HID
  reports from those rows.

## Snapshot Semantics

- Current matrix is a latest-state snapshot, not an event log.
- `current_seq8` increments when the local matrix snapshot changes.
- `host_known_seq8` advances on the HOST's answer to the snapshot request, and
  only under the gate in **Communication-Core Handoff** below.
- If a request is lost, PEER sends the latest current snapshot again later.
- Intermediate press/release edges are not guaranteed: an edge that comes and
  goes between two deliveries is not recoverable from this lane.
- **The accepted coalescing class**: a press and its release inside one
  delivery interval net to no change of state and are never observable to the
  HOST — a property of a latest-state snapshot, not a defect. The same class is
  accepted for the DUAL-HOST visual baseline, where such a pair lights nothing
  on the peer.

## HOST Cache

- Attach or relation epoch change invalidates the peer matrix cache; an
  accepted source-push carrying a matrix replaces it.
- Peer-row apply/clear follows from cache valid/seq changes plus the
  scheduler-supplied projection-active state. **Projection/apply count is not
  wire traffic.**

## Scheduler Boundary

- The first publishable local matrix snapshot marks matrix-ready dirty and
  schedules `SESSION_STATUS` revalidation so `matrix_ready` can be advertised.
  That same edge arms the PEER's standing exchange; the reason is canonical in
  `era_route_contract.md`'s grant section.
- Steady HOST-PEER PEER matrix dirty marks source-push route due and must not
  require full scheduler planning.
- **Source-push is the one runtime route core0 selects in this relation** — the
  response poll, the liveness heartbeat and the AUTHORITY push are core1's
  standing exchange — and it rides the same event route, rings, reserved
  capacity slot and priority as any core0 request. It preempts the standing
  exchange through core1's pass order rather than through a core0 predicate
  (canonical in `era_route_contract.md`).

## Communication-Core Handoff

`CORE1_FULL` is the only accepted communication-core stage. Core0 captures the
latest publishable source snapshot, packs the matrix bytes, and publishes those
bytes plus `matrix_seq` in an immutable request (`split/era_host_peer_matrix_link.c`).
**Core1 must not read the matrix engine or call a live-state source-push encoder.**

Core1 owns wire control/transaction sequence construction, adds the source-push
header around the copied snapshot, executes TX plus response RX, and returns
the request sequence, matrix sequence, result, and decoded response summary.
This lane's answer is the bare control ACK and carries no response section
(`era_route_contract.md`'s **One carrier for the response section set**), so
the summary is always empty here and core0 reads none of it.

> **REFUSED:** delete the empty response-summary return as dead code.
> **WHY:** the counters it feeds are the only measurement that the suppression is in force; a non-zero one falsifies it.
> **REOPENS:** a different measurement that still falsifies a non-empty summary.

Core0 advances `host_known_seq8` only after a generation-matching ACK result is
applied, and a newer local snapshot stays due rather than being hidden by an
older result.

On the HOST responder, core1 must reserve the dedicated source-push result slot
and copy the accepted packed matrix into it before sending the answer
(`split/communication_core/era_split_communication_core_responder_service.c`);
**no slot means no ACK**, and general session or heartbeat results must not
consume this slot. Core0 unpacks and applies the matrix only after
owner/relation/snapshot generations match.

## Diagnostics Interpretation

- Peer cache project count tracks cache copies used for engine apply, not
  matrix scan count.
- HOST-side `source_push_rx` and cache update count show accepted snapshots.
  HOST-only diagnostics cannot prove how many PEER-side matrix changes were
  never transmitted.
- `fail/bad/miss/ignored` are transport quality indicators.
- PEER-side `source_push_tx`, `source_push_ack` and seq convergence show sender
  progress; how that console line is read, including the `csp`-versus-matrix
  counter consistency rule, is canonical in `era_capture_reading.md`'s
  `wire csp` entry.
