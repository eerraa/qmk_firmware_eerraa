# ERA HOST-PEER Matrix Contract

Genre: contract
Canonical for: bounded HOST-PEER debounced snapshot history, separate latest
state, input acceptance and execution lifetime, and fallback convergence

Wire bodies and schema are owned by `era_wire_contract.md`; route priority,
FLOW and actual standing-stop recovery by `era_route_contract.md`; authority
admission by `era_authority_contract.md`; thread ownership by
`era_invariants.md`. Render snapshots in `split/era_host_peer_source_snapshot.c`
never write key-input state.

## Source, cache, and projection

`system/era_rp2040_matrix_core.c` publishes copied debounced local rows into
`split/era_split_input.c`. The PEER keeps bounded full snapshots and a separate
latest value. Wire DELTA batches and FULL fallback do not change that internal
snapshot meaning. A snapshot's simultaneous changes are never split into
separate groups, and collection never intentionally waits to fill a batch.

The sender preserves up to 18 snapshots whose oldest included born-to-actual-TX
age is less than 24 ms. An immutable pending request keeps its original born
deadline and logical identity across input-only ACK misses. The Core1 lane
checks that deadline, producer generation, owner/relation and standing stop
immediately before TX. Enqueue freshness is a different budget.

Core1 accepts up to two packets in a three-slot SPSC ring. All groups in a
packet share its acceptance time, and each must be claimed less than 4 ms
later. Capacity is reserved before validation/copy/shadow/receipt and
release-publication, which precede ACK. An exact duplicate compares logical
identity and complete body without the control byte; it does not enqueue,
XOR or refresh age. The receipt survives consumption and ACK transmission
failure. A mismatching body ends the input epoch.

The receiver's baseline uses a separate double-buffer mailbox, with at most
one unretired publication. Core0 publishes applied or retired-not-applied
before its storage may be reused. A retired baseline cannot be resurrected by
an identical RESET. Packets retain the read pin until the entire synchronous
claimed action pass finishes. Version, gate and age are checked again just
before claim; a reset before claim rejects it, while reset after claim lets
that group finish and invalidates later work. Input pins do not hold the
runtime response-plan generation or block general-result draining.

Only `split/era_split_input_qmk.c` admits a snapshot into the matrix engine's
cache/projection at the beginning of the action pass. It computes all changes
against actual `matrix_previous` before projection or action side effects.
Admission requires `remote queued + changes <= P`, with P equal to half-matrix
positions (66 on TOMAK), and HID capacity. QMK retains one waiting FIFO with
local quota 7 plus HP remote quota P; origin is captured before swap-hands and
retained through enqueue, pop and clear. FULL and RESET have no partial
consumption. A fresh baseline conservatively requires remote queued count 0.

## Invalidation and convergence

History full/expired, HID first overflow, receiver expiry, role/USB/storage
closure and input RESET close future history claims. They do not delete work
already accepted into QMK or a mixed local/remote HID prefix. Relationship
closure releases are themselves admitted as a complete group under the same
remote quota; they cannot overflow QMK by clearing projection outside that
seam. Local/tick/TD/HID work keeps progressing while capacity is unavailable.
A suspended HOST can request remote wake from pending input without admitting
it into action or advancing `matrix_previous`.

Ordinary terminal recovery captures latest into a new, strictly increasing,
nonwrapping epoch. Abandoned unconfirmed baselines may leave epoch gaps; a
terminal/live receiver accepts a strictly newer RESET, while a BIND-bound
receiver requires its bound epoch. Incarnation changes and epoch reuse require
FENCE-A/BIND-B. These boundaries never reuse a live packet/version holder.
If only the receiver incarnation closes, typed MISMATCH cancels the source's
old generation and restarts A/B before a baseline can install. A source
already waiting for FLOW or idle with held keys must also reach this recovery;
the closed receiver advertises REBIND in its existing standing answer even
without new input or changed USB facts. Baseline execution still waits for
capacity; FENCE/BIND does not claim action capacity.

Recovery converges to fresh latest and the last release when changes stop and
the clock, consumer, finite handlers and link progress. It need not preserve
edges abandoned outside the bounded history. A source-only ACK miss, input
expiry or saturation does not rotate the relation, restart storage auditing,
or produce SYNC/LINK presentation. Actual standing stop keeps its SESSION
recovery contract.

## Preservation limits

Within the admitted budgets, PEER snapshot order reaches successive action
passes, including short press/release and multi-key groups. This is bounded
snapshot history, not an unbounded key-event log. Original hold duration,
global ordering of both halves, tapping settlement time and USB/OS completion
are not preserved. QMK time is never rewound. Local quota overflow retains
QMK's legacy clear-all behavior and can affect already accepted mixed work;
the remote reservation does not promise to repair local overflow.

24/4 ms are implementation bounds, not measured performance guarantees or a
28 ms end-to-end bound. Timeout, ongoing transactions, QMK drain, tap width,
USB polling and OS processing remain separate. A nine-group packet requires
its first claim delay plus eight pass intervals to fit 4 ms. Actual pass and
USB timings require the device gates in `era_performance_gates.md`.

## Diagnostics

Source-owned counters at `split/era_host_peer_matrix_link.c` and the matrix
engine describe transactions, caches and projections, not every preserved
edge or host USB completion. Host proof lives in
`tests/era_split_input_history/` and `tests/era_split_input_qmk/`; physical
capture interpretation remains in `era_capture_reading.md`.
