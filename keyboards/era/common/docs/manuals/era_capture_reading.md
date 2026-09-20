# ERA Capture Reading

Genre: manual
Canonical for: capture prerequisites, sample-time model, units and denominators,
comparison windows, and cross-field interpretation needed to read ERA diagnostics
without turning source-owned fields into a second specification

Field spellings, enum values, bit masks, thresholds, and producer locations are
source-owned. Start a single-field lookup at `maps/era_identifier_map.md` and the
source it names. This manual owns the rules that combine multiple observations.
Acceptance figures and device pass/fail rules live in
`manuals/era_performance_gates.md`.

## Source Anchors, Not A Field Inventory

Use these as first owners, then follow the live producer for the field in
question:

| Diagnostic family | First source anchor |
| --- | --- |
| WIRE_DIAG snapshot and printing | `split/diagnostics/era_split_wire_diagnostics.c`, `split/diagnostics/era_split_wire_diagnostics.h` |
| scheduler/runtime diagnostic state | `split/era_split_transport_scheduler_diagnostics.h` |
| storage state and interval diagnostics | `split/era_host_peer_storage.c` |
| pass-phase/qwin phase ids | `system/era_pass_phase_diagnostics.h` |
| VIA macro interval diagnostics | `split/diagnostics/era_via_macro_diagnostics.c` |
| CLEAN transition breadcrumb | `split/era_split_restart_agreement.c` |

Route periods and admission semantics are owned by
`contracts/era_route_contract.md`; wire markers, bodies, and section eligibility
by `contracts/era_wire_contract.md`; storage durability and arbitration by
`contracts/era_host_peer_storage_contract.md`. Do not copy those tables here.

## Capture Preconditions

Use the diagnostic image and binding required by
`manuals/era_performance_gates.md`. Temporary bindings must be restored, and a
before/after comparison must use the same resolved diagnostic variant unless the
variant itself is the axis under test.

Name each sitting by relation and logical role, not only by physical half. Record
the manifest/build identity, link level, USB/cable topology, policy state,
lighting/suspend state, workload, and intended observation window. If one of
those is deliberately changed, hold the others fixed or state why the captures
are not directly comparable.

A `WIRE_DIAG` press is part of the observed execution. The snapshot is taken
from the keyboard task before later housekeeping, and printing is paced
afterward. Treat the trigger and console output as instrumentation, not as an
invisible observer. A visual state observed while capturing is therefore not a
pure counter measurement.

For an enforced-silence bracket, the frozen state can describe the pre-trigger
interval, but trigger-counted state may still move because of the capture key.
Prove "no workload" with an independent work/transfer observation rather than a
counter whose own boundary is opened by the trigger.

### Frozen snapshot versus paced live lines

Scheduler, authority, EEPROM, and storage snapshot families are frozen together.
Communication-core families are sampled again when their later lines are
printed. The exact line interval and line ordering are source-owned by
`split/diagnostics/era_split_wire_diagnostics.c`.

Consequences:

- Do not subtract a later live line from a frozen field as though both were read
  at one instant.
- If two facts must be simultaneous, use fields from the same frozen family or
  an explicitly shared timestamp.
- A clock value printed on a paced line includes the print distance from the
  frozen capture unless the producer explicitly compensates it.
- Missing output from a half that stopped running is missing evidence, not a
  zero-valued diagnostic result.

Several diagnostic families reset their interval baseline when a capture is
taken. In particular, relation-era counters and storage/VIA interval traces must
be read as the interval they already represent. Do not take a second delta of an
interval-reset field.

## Units, Denominators, And Windows

Before calculating anything, classify the field by its source semantics.

| Metric class | Valid reading rule |
| --- | --- |
| boot-cumulative count | subtract two captures from the same boot, then divide by the elapsed capture time only when a rate is required |
| role/capture-era count | already represents its own interval; do not difference successive captures |
| qwin/window count | use the window's own elapsed value and reset boundary |
| accumulated duration | divide the duration delta by the matching event/pass count for a per-event value, or by elapsed time only for an occupancy/share question |
| window maximum | read as an absolute maximum for that window; never difference maxima |
| histogram/band counts | normalize by the matching sample population and read the distribution, not only its mean |
| state/latch/mask | interpret as state at its sample instant; it is not a rate |
| timestamp/age | compare only values in the same clock domain or after applying the documented clock relationship |

For ordinary boot-cumulative rates, the elapsed denominator is the delta of the
capture timestamp on `wire pc`, whose producer is in
`split/diagnostics/era_split_wire_diagnostics.c`. Raw-matrix accumulated time
is not an elapsed-window substitute. A counter regression, boot-only timestamp
change, or source-defined reset invalidates a plain delta unless the source
explicitly defines wrap handling.

Keep units attached to the producer. Counts, microseconds, milliseconds, and
section counts are not interchangeable. A rate is meaningful only when numerator
and denominator cover the same interval and ownership era.

Treat a source-defined sentinel as unavailable/context-absent, not as a numeric
sample to average or difference. Likewise, preserve the producer's radix and
format: a decimal status value beside hexadecimal masks does not become hex by
proximity. Resolve the exact sentinel and format at the source anchor.

## Comparison Validity

A direct comparison requires all of the following unless the differing item is
the tested axis:

- same firmware/build identity and diagnostic instrumentation;
- same relation and logical role for the measured half;
- same link level and host/cable topology;
- same relevant policy, RGB/effect, suspend, and feature state;
- same workload definition and comparable observation-window length;
- no reboot, role rotation, or relation-generation boundary inside a cumulative
  delta;
- the same sampling family, or an explicit correction for paced-line skew.

LEFT versus RIGHT is not itself a valid comparison key. A role-scoped counter is
comparable only while each sample owns the same role. Off-role retained state is
history, not a current-role measurement.

Do not reuse a historical rate signature to identify the relation. Read the
captured relation/role state and then apply the current source and contracts.

## Reading A Cause Across Multiple Counters

For a cross-wire value, separate these legs:

1. local value or plan became eligible;
2. a carrier actually sent it;
3. the peer accepted the carrier;
4. the receiver policy admitted it;
5. the peer applied the value.

Use the source-owned counters for those legs. An advertisement counter does not
prove a send; a receive counter does not prove an apply; an apply counter does
not identify which carrier produced it without relation context. This distinction
is the default way to separate "didn't send", "sent but not applied", and
"applied".

Idle does not imply zero wire traffic. Standing liveness, refresh, discovery, or
time-anchor work may be valid in a serviced relation. A silence claim must name
the traffic class being excluded and use the counter that owns that class.

Runtime-section counters count sections, not polls or transactions. One carrier
can contain more than one section, so section totals and exchange totals do not
share a denominator unless the source explicitly makes them equivalent.

A source-defined measurement-valid bit or latch that is false means the
measurement is unavailable, not that traffic was zero. Likewise, a drained
queue/mask reading of zero cannot prove that no event occurred earlier in the
window.

## Storage And CLEAN Captures

Storage snapshots taken while an operation is open or active are intermediate
state. For end-of-operation conclusions, wait for the close/terminal boundary or
pair the observation with the other half's evidence as required by
`contracts/era_host_peer_storage_contract.md`.

Storage news and content-divergence facts answer different questions. A
forwarded news value can advance even when content later returns to its previous
value. Peer-news mirrors are useful for equality/convergence checks, not as
independent rates. Drained probe/push/conflict masks are transient state; zero
after drain does not prove that the path was unused.

Cause-timeline, storage-edge, and VIA-macro traces are interval-scoped. An
intermediate capture can rotate or clear the very interval being investigated.
For a bracketed operation, take the planned baseline and result captures without
injecting extra snapshots unless the test explicitly requires them.

Synchronous ERA NVM replacement can block Core0 between its begin/end
breadcrumbs. The absence of a keyboard-loop sample inside that bracket does not
prove that scan, routing, or persistence completed early. Wall-clock persistence
evidence and acceptance belong to `manuals/era_performance_gates.md`.

The live CLEAN line is a pre-reset breadcrumb. Decode its individual fields from
`split/era_split_restart_agreement.c`; judge ordering, bilateral preparation,
quarantine, and reboot-durable proof with the storage/route contracts and the
EEPROM CLEAN device gate. A final pre-reset print may itself be lost to reset, so
do not replace the gate with "last line was present".

## The shared clock and the time anchor

The wire contract makes the responder the time authority in both serviced
relations. The initiator applies the received anchor.

The capture timestamp is frozen once, while later runtime-section lines are
printed after paced delay. Therefore a raw difference between local capture time
and a later printed shared-clock value includes the print distance. Do not call
that difference an anchor offset without accounting for the sampling skew.

Anchor-apply count, correction information, and backward/truncation diagnostics
have different denominators. In particular, a truncation/backward counter is not
a one-for-one count of arrived anchors. No numeric pass threshold should be
invented from that ratio; any acceptance threshold belongs in
`manuals/era_performance_gates.md`.

## The qwin Window

`WIRE_QWIN` defines an explicit start/end observation window. Use the window's
own elapsed value for its rates and compare windows with matched relation, role,
variant, workload, and duration.

If the changed path is reactive to typing, rendering, or another active workload,
a quiet qwin does not bound that path. Exercise the workload that reaches the
changed path rather than promoting an idle window into a general result.

Core0 pass rate and any sampler/frame rate are different populations. Do not use
one as the denominator for the other.

Parked time is credited to the window in which the park ends. A park that begins
before the window can therefore make a short-window sleep share misleading or
even larger than the nominal window. Use occupancy/share only when boundary
parks are short relative to the window or when their boundary contribution is
explicitly accounted for.

Infer relation/role from captured state, not from a remembered loop-rate shape.

### The `qwin_phase` rung

A phase-instrumented window is not its own performance baseline. Compare it with
a plain qwin window of the same length and matched conditions.

Phase duration sums are per-pass only after division by the phase-pass
population. Window maxima are absolute values reset with the window, not deltas.
Threshold-band counts are nested and sensitive to samples near a boundary, so a
LEFT/RIGHT count difference is not sufficient evidence of a path regression;
inspect the corresponding maxima/distribution and the affected phase first.

Phase names and the current execution partition are source-owned by
`system/era_pass_phase_diagnostics.h`.

## Key-Path And Distribution Readings

The key-path diagnostic spans source-publish through acknowledgement/apply
boundaries defined by `split/era_host_peer_matrix_link.c`. Do not relabel the
entire span as key-to-host latency. Read its histogram as a distribution; an
average alone can hide a changed tail or a bucket-edge effect.

The exact bucket edges and producer fields are source-owned. This manual keeps
only the rule for comparing them: same population, same relation/role, same
window, and distribution before mean.

## Time, Reset, And Missing-Evidence Traps

Boot-only values must stay stable across captures from one boot. A change means
the pair is not one continuous measurement window.

Role/relation reset edges split ownership eras. Do not carry a delta across such
an edge unless the producer explicitly defines the field across generations.

A healthy survivor's zero failure counter does not prove that a peer never
wedged; a wedged peer may be unable to print its own failure state. Combine
survivor state, missing output, relation transition, and the relevant liveness
gate before concluding that a failure did or did not occur.

For response-section bytes or other overloaded diagnostic values, first verify
the response/message kind that gives the byte its interpretation. Decode the
bits from `contracts/era_wire_contract.md` or the source only after that kind is
known. A zero byte in the wrong kind is not an empty section mask.

## Reporting A Capture

A useful capture report states the build/manifest identity, relation and role,
topology, diagnostic variant, start/end condition, elapsed window, workload, and
the exact source/contract used to interpret any non-obvious field.

Report measured values as measured values. Do not turn source constants into
device measurements, do not promote one historical capture into a baseline, and
do not create a new threshold because two counters happened to differ once.
