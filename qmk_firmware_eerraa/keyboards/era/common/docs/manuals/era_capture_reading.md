# ERA Capture Reading

Genre: manual
Canonical for: capture prerequisites, sample-time model, units and denominators,
comparison windows, and interpretation of the minimal wire report

Field spellings, values and producers belong to
`split/diagnostics/era_split_wire_diagnostics.c` and its source owners.
Acceptance figures and device pass/fail rules live in
`manuals/era_performance_gates.md`. Route, wire and storage safety requirements
remain in their respective contracts.

## Capture Preconditions

Use the `wire` image and restore temporary bindings after measurement. Record
manifest/build identity, physical half, relation and logical role, link level,
USB/cable topology, policy, lighting/suspend state, workload and window.
Compare matching conditions and window lengths unless a difference is the
tested axis. A historical instrument result is a comparison input, not a
fresh measurement of this image.

`WIRE_DIAG` and `WIRE_QWIN` occupy their existing keycode slots and start and
stop the same window. The first pressed event starts silently; the next pressed
event of either key closes it. Releases are ignored. Triggers while output is
pending are ignored. `WIRE_DIAG_2` retains its numeric slot but is inert; it
does not inject a fault. The trigger is part of the observed execution.

No output is printed during the window. After it closes, the compact report
prints at least 20 ms apart: `wire scan`, `wire core`, `wire link`, and, when
storage is enabled, `wire storage`. Captured values stay frozen across those
lines. Individual counter reads are aligned but do not constitute an atomic
multi-Core1 snapshot. Missing output is missing evidence, not zero.

After these unchanged lines, `wire cause` identifies the detail schema,
available slots, occupied slots and `unavailable` count. One `wire detail`
line per occupied slot follows at the same pacing. The ledger is frozen with
the report, not read again while printing. This extends the one `wire` variant;
it neither changes the trigger/keycodes nor creates another profile.

## Reading The Window

`wire scan` reports elapsed milliseconds, Core0 scan count and rate, PIO
validity and frame rate, sampler retry and rearm deltas, and the PIO debug
flags. Core0 scans and PIO sampler frames are distinct populations. PIO rate
is valid only if the sampler is ready at both endpoints, the same nonzero
frame-word count applies, and no DMA rearm occurred during the window.
An invalid rate of zero is flagged unavailable, not a measured zero.

`wire core` reports relation mode and owner health; `wire link` reports compact
transaction health; `wire storage` reports storage state/status and failure
counts. Health counts are cumulative since their owner reset or boot, not
automatically window-differenced. Compare two captures only within the same
owner lifetime, accounting for resets and wrap according to the producer.
States are samples, not event counts. A healthy survivor's counters cannot
prove that a silent peer never wedged.

Idle does not mean zero wire traffic: standing liveness and discovery may be
valid. A transmission does not prove peer acceptance or application. Storage
state observed during an operation is intermediate; convergence of a public
value alone does not prove lifecycle release or durable completion. Use
independent peer, protocol, persistence or device evidence when those facts
must be established.

The report does not expose park/CPU occupancy, phase durations or maxima,
keypath histograms, cause timelines, edge traces, macro RAW timing, CLEAN
traces, or the former timing/state fixtures. A required device gate without
adequate evidence remains unverified;
absence of an old field is not a pass.

## First-cause detail (schema 1)

`split/diagnostics/era_split_wire_cause.h` owns the dense cause and phase
numbers; they are capture identifiers, not wire operations. Decode them with
the image's manifest/source revision. The inventory covers local compact
encoder arguments/control, compact and bulk checksum versus payload
classification, RX marker/length/capacity, direction, response kind, ACK, and
simultaneous kind/ACK failure. Wait causes distinguish original send timeout,
initial response timeout, partial frame, RX cleanup timeout, PIO, IO, and each
administrative owner/epoch/cancel/reset interruption. Shared causes pool
occurrences across phases; the first record preserves its exact phase, not
the distribution of later phases. Impossible codec entry predicates are not
allocated slots. Unattributed counted failures have an explicit unavailable
slot rather than inheriting a preceding attempt's detail.

The ledger starts at the first Core1 counted transaction and resets only on
firmware reboot. It is not an event ring: administrative interruptions cannot
evict a checksum or ACK record. Each slot keeps its first immutable record and
a saturating 32-bit `n`; saturation means at least that many occurrences.
Owner transfers, relation rotations, rate changes and captures do not clear
it. Only the existing engine MISS/BAD/FAIL counting point commits a record.
Uncounted early returns and idle receive timeout remain uncounted. RX still
increments before the response kind/ACK check; a BAD count does not by itself
mean a bad checksum or prove that the counter is bogus.

Read detail fields as follows:

- `us`: RP2040 hardware `timerawl`, microseconds modulo 2^32 (wrap about
  71.6 minutes), sampled on the rejection path. It is neither QMK uptime nor
  the relation's synchronized clock. Zero is valid; timestamps alone cannot
  order records across a wrap or different halves.
- `ep`: attempt/observed owner epochs. `rel`, `mode`, `why`: epoch-tagged Core0
  context sampled with a bounded odd/even publication read. `why` is the last
  requested scheduler dirty-flag mask, or `0x80` for a requested quiesce via
  the flash-write boundary (also used by link transitions). It is concurrent
  context, not proof of the reason for the failure. Relation rotation may
  preserve an owner lease; these identities are not interchangeable.
- `v`: validity bits named in the cause header: context, wait, original send,
  cleanup and RX PC. Ignore context values without context validity, including
  when Core0 was concurrently publishing or its epoch did not match. Failed
  context reads never block transport. `role` and `baud` are backend facts,
  not live USB/QMK/EEPROM reads.
- `ph` and `end`: rejection phase and terminal engine result/failure.
  `wait` is the rejection's backend wait result when valid. `send` and `clean`
  have separate validity and retain the original send-stage result and RX
  turnaround result. A failed send followed by successful cleanup can still
  produce the existing generic IO terminal failure: instrumentation preserves
  that functional behavior deliberately.
- `pc`: RX state-machine PC/program start immediately before the existing
  failed-send forced recovery, when available. Cleanup result describes
  `enter_rx_state_owned`, not an electrical verification of force-RX.
- `len`: rejection-specific operands: frame/payload lengths for codec
  rejection, received/allowed length for length rejection, required/available
  bytes for capacity, or expected/observed direction. `flags` holds only the
  framing/control byte, or null-argument bits for local encoding. No key
  matrix, macro text, storage body or other payload contents are retained.
- `kind`: expected/alternative/observed classifier kinds; `seq`: request TX
  and received ACK. Separate kind-only, ACK-only and both-failed slots preserve
  both predicates even when the functional check short-circuits.

First-record data is release-published before an aligned nonzero count;
Core0 acquires the count before copying immutable data. A halt before that
publication cannot expose a partial record. Counts and old health totals are
independent reads, not an atomic capture. Successful traffic takes no new
timestamps, payload copies, publications or per-frame counters; the wire image
does have private attempt bookkeeping and must not be called cycle-identical
to standard firmware. Standard/reset compile out the new ledger and observers.

This is attribution evidence, not an electrical diagnosis. In particular a
force-RX PC sample does not establish that an ISR/PC mismatch caused a physical
error. Neither this feature nor a quiet capture reproduces the original errors.

## The shared clock and the time anchor

The responder is the time authority in both serviced relations and the
initiator applies the received anchor. The minimal report does not expose
held-time correction or anchor timing. An offset or correction claim requires
independent evidence in the appropriate clock domains.

## Reporting A Capture

Report measured values with their units and source/contract interpretation,
including start/end conditions and the workload. Exercise the relevant active
path; a quiet window does not bound typing or rendering. Do not infer device
timing from constants, invent thresholds, or promote an old capture to a
measurement of the current image.
