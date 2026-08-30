# ERA Route Contract

Status: active
Genre: contract
Canonical for: route ownership and priority, route kind/reason, all service
cadences and poll period values, the standing exchange grant, storage route
admission and exclusivity, freshness handoff, stale recovery, the due/deadline
model, and latch discipline
Read when: editing router, scheduler, responder service, or mode planner

## Common Route Priority

1. Required attach/status bootstrap, policy, or recovery revalidation when due.
2. Active exclusive storage route while its relation remains valid.
3. Mode-specific normal route.
4. No-op.

Storage is exclusive against normal matrix/heartbeat/HOST-source-poll traffic,
not against mandatory relation recovery. This ordering is the bounded-idle
proof that storage cannot hide cable loss, role change, policy change, or stale
session authority indefinitely.

**Priority applies at transaction boundaries; an exchange already begun is
never preempted.** Core0 therefore admits only one of the generic request ring
and the dedicated storage request slot at a time. A pending generic request
prevents the next storage publication, as before; the reverse gate now holds
both `SESSION_STATUS` and matrix outside the generic ring from successful
storage publication through result consumption or cancellation. The preceding
reserve-to-publish prefix has no scheduler point inside its one Core0 call.
Their due facts remain armed. Thus a status edge
that arrives during one storage transaction runs after that result is drained
and before another storage publication, while matrix runs only after the same
higher-priority work. The admission reads the existing Core0 cached
request-pending flag and adds no queue or state (`split/era_host_peer_storage.c`,
`split/scheduler/era_split_transport_scheduler_routes.c`).

**A Core1 request's queue freshness begins at shared publication, not request
construction.** Once the two request slots cannot coexist, the only legal
predecessor is one already-running compact standing exchange. The not-after
window is therefore the configured standing response window at the current
backend wire scale plus a 5 ms handoff/TX margin. The default 20 ms response
window yields 25 / 45 / 85 ms at High / Medium / Low. The margin is statically
held above one maximum compact frame plus turnaround at Low, and the configured
Low-level maximum must remain below the 100 ms Core1-unresponsive judgment or
the build fails. This is a queue-residence bound only; the selected request's own
response window starts after BEGIN (`split/scheduler/era_split_transport_scheduler_routes.c`,
`split/communication_core/era_split_communication_core_host_peer_lanes.c`,
`split/communication_core/era_split_communication_core_storage.c`).

**The exclusive span is the transfer phase, not the episode.** Exclusivity runs
from a validated `TRANSFER` to each role's transfer-verified boundary and to an
abort's bounded cleanup; the apply phase is a local flash operation on the
receiving half. ERA NVM performs no recursive keyboard/wire pass from inside a
program or erase primitive; Core1's standing exchange keeps relation liveness
alive while Core0 is synchronously unavailable. The per-role transfer-verified
boundaries and close/recovery rules are canonical in
`era_host_peer_storage_contract.md`. The flash window is therefore a measured
Core0 outage, not a set of route-admission gaps a route may plan to occupy.

**CLEAN quarantine is a different and stronger admission state than transfer
exclusivity.** A half installs the cached O(1) gate before it publishes CLEAN
PREPARED, and it stays installed through COMMIT and controlled reset. While it
is set, this half may neither select nor publish a storage request, admit or
answer one as responder, or expose a storage snapshot for a new episode. Any
already-owned storage request/result reaches its existing safe close/cancel
boundary before PREPARED may publish. Mandatory
`SESSION_STATUS` and the standing exchange remain admitted: AUTHORITY and
`RESTART_ARM` are the carriers that finish the CLEAN agreement. No abort,
timeout or relation rotation clears the gate; a new relation re-drives the
local monotonic prepare obligation and only controlled reset clears the live
state. The state adds no route, queue, scan, cadence or wire fact
(`split/era_split_restart_agreement.c`, `split/era_host_peer_storage.c`,
`split/scheduler/era_split_transport_scheduler_routes.c`).

**Exclusivity suppresses the routes an initiator selects and never a
responder's answer.** A busy responder that goes mute is indistinguishable from
a dead wire, and the initiator's answer to a dead wire is to tear the relation
down, so a responder answers the admitted slot under exclusivity and carries no
section when it does. Nothing crosses that could not before: the response plan
is suppressed for the transfer, so the slot is answered with the section-less
ACK by construction.

**The shape that keeps both relations implementing it: the relation fact and
the payload admission derived from it are separate terms.** A HOST-PEER HOST's
heartbeat admission is the relation fact — confirmed HOST-PEER HOST with a
session that admits its PEER's matrix — and carries no exclusivity term. Matrix
admission is that same fact AND'ed with exclusivity, because rows accepted
mid-transfer are rows the exclusive close is about to discard. Deriving the
first from the second makes a busy HOST refuse the section-less ACK, and that
refusal is not survivable: the standing exchange's liveness beat runs *through*
an apply, so it meets the refusal on every frame, stops, and reproduces inside
the grant the collapse this rule exists to prevent.

## Route Kind And Reason

Route kind is the selected wire operation. Route reason is the initiator-local
service cause for running that wire operation now. A route reason is not a wire
payload and is not automatically visible to the peer: the responder must rely
only on the admitted wire operation and its own dirty state when deciding the
response body.

Active route reasons:

- `ATTACH_STATUS_REVALIDATION`: `SESSION_STATUS` bootstrap, policy, discovery,
  liveness, or recovery revalidation.
- `HOST_PEER_MATRIX_SOURCE_PUSH`: PEER matrix source-push event or forced
  baseline. Event/due driven, with no cadence at all.
- `HOST_PEER_STORAGE_IDLE_PROOF`: initiator-initiated nonexclusive proof probe
  for a portable storage domain selected by the relation-open audit sweep or a
  responder-changed hint, including the nonexclusive `BUSY`/core0 pin retry.
  Both halves of the identifier name are historical: there is no periodic idle
  patrol, and the lane is not HOST-PEER-only.
- `HOST_PEER_STORAGE_ACTIVE`: initiator-initiated chunk/apply/complete steps
  after validated `TRANSFER`, plus bounded abort cleanup after storage takes
  exclusivity.
- `RUNTIME_SECTION_PUSH`: a local runtime section differs from what the wire
  last confirmed. The wire operation is `HOST_PEER_SOURCE_PUSH` carrying a
  runtime section mask instead of the matrix.
- `RUNTIME_RESPONSE_POLL`: the initiator opens a slot so the responder can
  answer, which is the only way it ever speaks. The wire operation is the
  existing `HOST_PEER_HEARTBEAT`: the responder sees an ordinary heartbeat and
  chooses its response body from its own dirty state.

**Core0 selects neither runtime reason, in either relation.** Both belong to
core1's standing exchange, which stamps `ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT`
with one of them so an `rk`/`rr` pair in a capture keeps its meaning. Only that
kind survives the grant and keeps its value; the kind and the two core0 service
reasons it replaced were deleted with the captures that needed them, so every
value in both enums now has a producer (`era_identifier_map.md`,
`split/era_split_wire_router.h`).

Default service cadences:

- **`SESSION_STATUS` has no post-relation cadence and no in-relation edge.**
  Once a relation's own lane is live the periodic beat has nothing left to
  revalidate — the lane carries liveness through accepted relation traffic and
  the peer's session facts in the AUTHORITY section (`era_wire_contract.md`) —
  and neither does an authority edge inside that relation, for the same reason;
  one predicate answers both suppressions in source, because they are one
  claim. Which edges still raise a pending revalidation is canonical in
  `era_authority_contract.md`.

  What the frame keeps is discovery, bootstrap and recovery — every one an edge,
  every one a frame this half sends because it needs an answer, reachable
  through the pending-revalidation arm that outranks every other route, and a
  missed one therefore decisive with nothing softening it.
  `ERA_SPLIT_SESSION_REFRESH_PERIOD_MS` (50 ms) is the period a known peer's
  periodic status runs at *while that relation's own lane is not live*; a live
  lane suppresses it outright
  (`scheduler/era_split_transport_scheduler_timing.c`).

  **No half may reintroduce a core0-originated cadence inside a live
  relation** — not for a keepalive during a sliced write (core0 inside a flash
  write is precisely what cannot send one; core1's liveness beat below holds
  the wire instead), and not to poll an advisory bit (a fact a relation needs
  rides that relation's own response section).
- **Nothing core0 originates runs on a cadence in either relation**, so the
  half that can stop is out of the liveness path in both. **What differs
  between the relations is not the cadence**: HOST-PEER carries a matrix and
  DUAL-HOST does not, so HOST-PEER's PEER keeps one core0-selected route, the
  matrix source-push, and the two relations run different section sets on the
  same envelope (`era_wire_contract.md`). One runtime architecture is a claim
  about *when* an exchange runs and which core decides it, not about what
  crosses.
- The PEER sliced apply-write window runs no periodic `SESSION_STATUS`; core1's
  liveness beat holds the wire through it in both relations.
  `ATTACH_STATUS_REVALIDATION` still passes the storage route-exclusive mask,
  because a revalidation must reach a peer during an episode — what it no
  longer does is run on a cadence there.
- `ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS` (10 ms) is the HOST-PEER
  standing exchange's period; it keeps the name of the core0 response poll the
  grant replaced. **Owner decision 2026-08-09 halved it from 20 ms.** What
  licenses a shorter period is the Due/Deadline Model's rule below — no runtime
  route deadline sits in the core0 deadline set — so shortening it costs core0
  no wake. The decision was taken without the saturation figure the retired
  backlog entry named as its precondition, on worst-case arithmetic instead,
  so what settles it is a reading rather than a build: the runtime lane's
  poll-driven counter judged against this period's reciprocal
  (`era_capture_reading.md`).
  `ERA_HOST_PEER_RGB_STATE_SNAPSHOT_PUBLISH_PERIOD_MS` is *defined* as this
  value and arms on the DUAL-HOST Right too, so this period is the one core0
  wake still tracking a poll period in either relation — a consequence that
  does not read off the period and that no build can check.
- **`ERA_SPLIT_PEER_RESPONSE_WINDOW_MS` (20 ms) is not a cadence and does not
  follow the period.** The window a single exchange may wait now exceeds the
  period between exchanges, and what keeps that from piling up is the standing
  loop's own stop rule rather than arithmetic: a non-OK exchange latches the
  initiator's `stopped` flag and core1 originates nothing further until core0
  republishes the plan, so a timed-out exchange costs one window once and not
  one window per period. The deadline is re-armed from the instant the poll
  decision was taken, not from the exchange's completion — the same reason a
  slow exchange cannot accumulate a catch-up burst, and the reason an exchange
  *longer* than its own period removes the cadence outright rather than
  degrading it.
- **A new service reason must define its own wire surface, ownership, and
  cadence before execution opens for it.** Reusing a lane is not defining one:
  DUAL-HOST storage defined no new service reason because it reuses both
  storage reasons and their lane, and the two runtime reasons above cover every
  runtime section either relation carries — which is why adding a section adds
  neither a reason nor a cadence.
- **The DUAL-HOST runtime cadence is one period, run unconditionally** (owner
  decision 2026-08-01). `ERA_SPLIT_DUAL_RUNTIME_POLL_MS` (1 ms, halved from
  2 ms by owner decision 2026-08-09) is due from the moment `SESSION_STATUS`
  confirms the relation, with nothing gating it: no activity window, no quiet
  rate, no activity hint in either carrier. A local section going dirty is due
  immediately and carries no cadence at all, the treatment matrix dirty gets.

  **This period is the responder's latency bound and nothing else's.** The
  initiator can speak whenever it likes — publishing the plan sends an event, so
  its layer edge reaches the wire in about one scan whatever the period is — but
  the wire is half duplex and the initiator owns it, so a responder-side change
  waits for the next poll and for nothing else.

  **The period is not measured and its bound is the wire, not core0** — the
  claim the standing grant exists to make true, and falsifying it is the lane's
  first measurement. **One half of that claim was measured on 2026-08-16 and it
  needed a correction**: the initiator's core1 was not merely waiting on the
  wire, it was *busy-polling* it, awake 258 µs an exchange. Parking those waits
  on WFE cut that to 160 µs, one park per wire byte, and the half's scan rate
  recovered 2–3 % of the cabled deficit. The resulting core1 sleep fractions are
  standing figures with one home, `era_performance_gates.md`'s **Fixed
  Baselines**. So the cadence's cost to core0 was
  partly core1's own spin rather than the wire, and the sentence above is now
  true of the parked implementation rather than true by construction. The
  wire's saturation rate is still unmeasured: the
  figure derived from byte time excludes the half-duplex turnaround and the
  responder's own claim-plan-encode-send, so it bounds something that is not
  the ceiling. **At 1 ms that bound stops being remote**, which is the whole of
  what the 2 ms → 1 ms decision changed: a worst-case exchange is roughly
  0.80 ms, most of the period rather than a fraction of it, and the period holds
  only while the exchange fits inside it, because the due deadline is anchored
  to an exchange's *start*, not its completion. An exchange that overruns
  re-arms a deadline already in the past and core1 free-runs at whatever the
  exchange costs, with nothing clamping it, so the cadence stops being a cadence
  rather than degrading. **Every `_Static_assert` on a poll period is an upper
  bound against the responder's silence limit, so a period that is too short
  cannot fail a build.** The one precedent for that failure is the retired
  activity-gated two-rate cadence, whose one-shot latch was armed from a level
  rather than an edge and made the poll period unreachable while the initiator
  polled as fast as its loop allowed; that unreachable-period shape is what a
  device check on this value watches for, and it is read as a poll rate far
  above the configured period on the wire counters (`era_capture_reading.md`).
- HOST-PEER storage has no periodic idle-proof cadence. Probes are scheduled by
  the mandatory relation-open seven-domain audit sweep at the 25 ms storage
  retry cadence, and **by the whole-family summary the news value arms** per
  `era_host_peer_storage_contract.md`. The hint schedules no probe of its own:
  it names no domain, so it cannot; it arms a summary, and the summary's own
  result decides the direction and the domains. A 1000 ms trailing local storage
  quiet deadline gates dirty source capture, with a flat 25 ms retry deadline,
  a failure streak bounded by the derived peer-silence window
  (`ERA_HOST_PEER_STORAGE_PEER_SILENCE_MS / RETRY_MS` consecutive failures —
  the derivation is `era_host_peer_storage_contract.md`'s), and a 5 s episode
  deadline. Repeated probing of one
  domain is separately suppressed by an exponential probe backoff —
  `ERA_HOST_PEER_STORAGE_RETRY_MS << streak`, streak capped at
  `BACKOFF_MAX_SHIFT` (6), delay capped at `BACKOFF_MAX_MS` (1000 ms) — so the
  two are distinct mechanisms and only the second is a backoff. These cadences
  are live wherever the replacement storage engine is compiled in, which is
  derived from `ERA_SPLIT_EEPROM_SYNC_ENABLE` rather than from board identity
  (`era_source_map.md`).

## Runtime Execution Owner

**Core0 never polls core1.** Core1 publishes, and core0 is woken by the edge —
a ready initiator result, or the standing state's change sequence. Those two
carry different things and never overlap: the result carries lane facts and the
standing state carries the response section set (see **One carrier for the
response section set** below). The shape this replaced is a flag read both by
the scheduler's housekeeping-due gate and by the lane's own drain: that pair
ran a full housekeeping pass per matrix *scan* for the whole in-flight window,
rebuilding state that had not changed.

One case remains that core1 cannot announce, and it is an error path: a request
core1 consumed without publishing a result, because the result ring was full.
It unsticks on the next authority sample rather than through a poll, since that
sample's deadline is always in the scheduler's deadline set.

The responder side has one bounded exception to per-arrival result publication.
While Core0 is unavailable, Core1 may answer repeated section-bearing
HEARTBEATs from the same immutable responder snapshot. Once one successful
result for the exact owner/relation/snapshot/section-mask tuple is pending, later
successful HEARTBEAT replies do not reserve another general-ring slot: their
only Core0 effect would be the same sent-shadow commit. Physical traffic is
retained in monotonic bulk-fold counters, so ACK/projection/runtime-section
diagnostics remain counts of what crossed. SESSION and both push result kinds
never coalesce because they can carry peer input. Device evidence that opened
this rule was the 2026-08-30 DUAL-HOST macro Apply: a ~125 ms responder Core0
flash window accumulated `crsp full=32/noack=45` while the initiator's pending
fall sat behind SESSION_STATUS revalidation for 115 ms.

Route authority and runtime execution ownership are separate. Core0 selects
routes and publishes immutable inputs; moving a transaction to core1 does not
change which physical half may initiate it or what the peer may send. Backend
role/owner epochs may move core1 between initiator, responder and disabled
states, but ownership never switches per route or to core0. Which core owns
what — the `CORE1_FULL` stage rule, and the core0 executor, lease, wait-adapter
and fallback forms that are forbidden — is canonical in `era_invariants.md`.

### The standing exchange grant

**One route kind may run on core1's own period, in every serviced relation.**
Core0 grants it by publishing an exchange plan; core1 runs the exchange while
the grant holds and publishes a latest-state result. Nothing crosses the core
boundary per exchange. The grant covers the `RUNTIME_RESPONSE_POLL` and
`RUNTIME_SECTION_PUSH` reasons and nothing else — which is the whole of what
either relation's initiator originates on a cadence.

**The period is the only per-relation term**: DUAL-HOST polls at
`ERA_SPLIT_DUAL_RUNTIME_POLL_MS` (1 ms) and HOST-PEER at
`ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS` (10 ms). Everything else here
is one rule for both relations, and that is the claim rather than a convenience.

**DUAL-HOST's period is also the one term that follows the link level**, at
1 / 2 / 4 ms for High / Medium / Low. The scheduler computes it at the plan
build as `ERA_SPLIT_DUAL_RUNTIME_POLL_MS × era_split_transaction_backend_wire_scale()`
(`split/era_split_transport_scheduler.c`), asking the backend rather than the
link unit: **the backend holds the baud and every number derived from it, and
the link unit answers no scale at all.** One fact with two derivations is what
that replaced, and the failure it made possible was the poll period and the wire
timeouts disagreeing after only one of them had moved. Wire time is exactly inversely proportional to baud, so doubling
the period at half the rate holds the worst-case exchange at the same share of
its own period — and slightly better than the same share, because the
exchange's CPU part does not scale with the wire: at 1 ms and 460800 the
worst case is ~0.80 ms with 0.2 ms of guaranteed idle, and every step down
leaves more. Core1's mean load is likewise flat. **HOST-PEER does not scale**:
10 ms has room, and slowing it would spend the responder's boarding latency to
buy margin that was never short.

**HOST-PEER's arm carries one precondition the grant's three identities do not
express, required in both directions**: the PEER builds no plan until its own
matrix is ready. The HOST answers a heartbeat only once its session records
this PEER as `matrix_ready` (`era_invariants.md`), so a grant issued earlier
would poll a responder that refuses. Reaching `matrix_ready` raises a status
revalidation, which clears the enable bit until the frame carrying it has
round-tripped — which hands liveness from the `SESSION_STATUS` beat to the
standing exchange with no gap, since the beat is suppressed by the same
`matrix_ready` term one predicate later.

Why this shape rather than a queue: the request/result rings are right for a
one-off and wrong for a subscription. Under a constant cadence a queue makes
core0 pay per exchange — worse, per *scan* of the in-flight window, because the
housekeeping due predicate reports work for its whole duration. The grant gives
the initiator the shape the responder already has, where one published snapshot
serves arbitrarily many requests.

The rules, and each is a bound rather than a convention:

- **The plan is published on change, never on a pass or a timer.** It carries
  the owner epoch, the relation generation, an enable bit, the period, the
  eligible push section mask, and the latest-state section bodies — the
  restart phase among them, whose bounded PREPARE/COMMIT/idle changes are
  exactly the cadence class this rule was built for, and whose nonzero commit
  deadline is an absolute instant so a republish hands core1 the same body
  rather than a moved one. Publishing
  it on a pass would reintroduce the per-exchange core0 cost the grant exists
  to remove, and publishing a time-varying field in it would make every publish
  differ — the failure the responder snapshot already paid for once.

  **A plan field is filled only where this relation's push eligibility carries
  it** — the same rule from the publisher's side, since a field that moves on
  its own turns "on change" into "on pass". `INPUT_LAYER` is the case it is
  written for: HOST-PEER does not carry that section, so filling the byte there
  would republish the whole plan on every layer edge of that half, for a byte
  the relation discards.

  **The publication route-due bit is DUAL-HOST's alone for the same reason.**
  That bit is what `scan_idle()` (`split/era_split_transport_scheduler.c`) reads, so it exists to put a layer edge on the
  wire in about one scan: the layer hook ORs it as a prompt, and its lifetime
  ends at the publish it asked for. Every field of a HOST-PEER plan is produced
  at housekeeping cadence — the authority sample, the mode pass, the storage
  task, the status round trip — so nothing a scan does can move one, and arming
  the bit there would hold `scan_idle()` false and run the slow path per scan
  until the publish: exactly the per-exchange core0 cost the grant takes off
  that half. The housekeeping backstop publishes a HOST-PEER plan within the
  authority sample's own deadline.

  **The bit is consumed where the publish answers it**, at the top of
  `era_split_transport_scheduler_publish_standing_plan()`
  (`era_split_transport_scheduler.c`), and before the plan is built so that a
  mark arriving during the build re-arms it. The housekeeping recompute
  (`scheduler/era_split_transport_scheduler_timing.c`,
  `era_split_transport_scheduler_refresh_route_due_flags()`) is the backstop
  and not the consumer: the scan path's own call to it is gated on a route
  having been selected, and this is the one route-due bit that selects none.
  Without a consumer at the publish a DUAL-HOST Left kept failing
  `scan_idle()` and running the slow path per scan until the next housekeeping
  pass, which is what that arrangement costs.
- **The result is latest-state and not a queue.** Core1 overwrites it, so core0
  reading it late costs latency and never correctness, and a core0 that reads
  it rarely loses nothing. Besides peer latest-state and the stop flag, it
  carries the initiator's last successfully sent `STORAGE_PENDING` value as a
  local confirmation edge; that edge exists only so the STATUS presentation can
  hold a local fall until its zero really crossed, and therefore wakes Core0
  only when that section changes rather than once per poll
  (`split/communication_core/era_split_communication_core_standing.c`,
  `split/era_split_transport_scheduler.c`). The responder needs no second
  cross-core record for the same rule: its ordinary result already returns the
  actual response section mask and plan to Core0, and the existing sent-shadow
  commit is the successful `STORAGE_NEWS` confirmation
  (`split/scheduler/era_split_transport_scheduler_responder.c`). A queue here would restore
  per-exchange work on the drain side after removing it on the submit side.
- **Publication uses the responder snapshot's discipline**: an odd/even publish
  sequence with a claim word, core1 claiming across the exchange. One
  discipline for both published records, not two.

  > **REFUSED:** publish the response plan inside the responder snapshot
  > instead of threading it.
  > **WHY:** it lowers and re-raises the same table across the core boundary.
  > **REOPENS:** a snapshot that already carries the plan for another reason.
- **The grant stops on any of the three identities changing** — owner epoch,
  relation generation, enable bit — and core1 must observe that within its own
  loop period. Relation rotation bumps the generation; storage taking
  exclusivity clears the enable bit; a wire-role change moves the epoch.
- **The enable bit stops the cadence and does not stop liveness.** While it is
  clear but the other two identities hold, core1 still runs one section-less
  exchange whenever nothing has crossed the wire for
  `ERA_SPLIT_STANDING_LIVENESS_MS` — half the responder-silence limit, the same
  margin the poll periods are asserted against, and not a per-relation value
  because the limit it halves is a constant of both. It carries no section,
  ever: exclusivity suppresses content, and this is the frame that proves the
  half is alive rather than one that moves state.

  **It exists because the enable bit hands the wire back to core0 at exactly
  the moment core0 is about to be unavailable.** An exclusive episode's only
  other traffic would be a core0-originated keepalive, and core0 inside the
  flash write is what cannot originate one; a durable apply outlasts the peer's
  100 ms watch by more than an order of magnitude, so the episode would end in
  an abort into a peer that had already forgotten the relation. The beat runs
  from the core that cannot be inside a flash write.

  It costs nothing when the wire is busy, structurally rather than by tuning:
  accepted relation traffic refreshes the peer's watch regardless of class
  (`era_invariants.md`), and core1 reaches the standing service only on a pass
  where the request queue and the storage lane both did nothing, so the beat
  cannot contend with a transaction. A stop latched on a failed exchange stops
  the beat too: failure returns the wire to core0's `SESSION_STATUS`, which
  outranks this, and a beat that resumed on its own would be the retry the
  grant forbids.

  **A stop inside a durable Apply remains a relation failure, not a persistence
  rollback condition.** ERA NVM does not return to core0 between page programs
  merely to feed the route. If the standing exchange stops during the
  synchronous Apply, the local NVM call still finishes to old-or-new authority;
  when core0 returns, the existing `ATTACH_STATUS` revalidation path outranks
  normal traffic and repairs the relation from whichever durable state won.
  Device acceptance therefore measures standing liveness and failure counters
  across the complete NVM window rather than relying on a source-derived
  sub-window bound.
- **Failure is core0's, and the property to preserve is route priority rather
  than a clock.** Core1 stops the standing exchange on the first failed
  transaction and reports it; it never retries one on its own. Core0's ordinary
  stale/revalidation path then runs, and `ATTACH_STATUS` outranks the runtime
  route, so a doubtful relation goes back through `SESSION_STATUS`; a
  reconfirmed relation re-publishes the plan enabled. Reading this as a clock is
  a level off: what protects a dying link is not that the period clock stops
  advancing — the cleared clock makes the next poll due immediately — but that
  the failure raises pending status and status wins.

  **A stop raises the revalidation and does not declare the peer stale.** The
  stopped exchange knows only that nothing came back — exactly what a busy
  responder and a dead wire have in common — so it is not entitled to decide
  between them; `SESSION_STATUS` is, and is already the frame this hands the
  wire to. A peer that answers holds the relation with no rotation, and a peer
  that does not is marked stale by the same missed-status rule that covers every
  other lost `SESSION_STATUS`. Declaring staleness at the stop instead turns a
  single refused poll into `rel=0` with the peer forgotten; the storage lane's
  own close uses the same shape, raising a revalidation and letting the frame
  decide.
- **Core1 still reads no live QMK state.** What crosses is transaction work, not
  state: core0 publishes the section bodies and core1 serializes them, which is
  what makes "transactions belong to core1" and "core1 reads no live QMK state"
  hold together rather than trade against each other.
- In HOST-PEER, PEER core1 sends the request and receives the response; HOST
  core1 receives the request and may send only its admitted ACK/HSRSP response.
- Peer-unknown discovery remains Left-initiated `SESSION_STATUS`; moving its
  executor to core1 does not authorize Right to probe independently.

#### One carrier for the response section set

**A response section rides the standing exchange's answer and nothing else.** A
request from the initiator's core0 lane — HOST-PEER's matrix push, the only
thing core0 still queues besides the revalidation frame — is answered with the
bare control ACK and carries no section at all. The relation's eligibility table
is untouched by this: it narrows *when* a section crosses, never *whether* the
relation may carry it.

The rule is scoped to the initiator direction, and the reason is what it
protects. The initiator's core1 caches what it received so it can report an edge
rather than waking core0 at the poll rate. A second arrival path puts a value
into core0's appliers without passing through that cache, and because a
sent-state carrier crosses each value exactly once, the cache then holds the
*previous* value permanently. Two failures follow: the next legitimate
re-assertion of the held value raises no edge and never applies, and the record
republishes the held value over a correctly applied one on every unrelated
edge, because core0's apply re-reads every valid field whenever the change
sequence moves.

**The generalization, and it is the one this lane earned:** when an edge filter
stands between a carrier and its consumer, the carrier must have exactly one
path to that filter. A second path is not a shortcut around the filter — it is a
way to make the filter lie. Sealing each bypass as it is found is the shape this
project has already paid for twice.

The responder direction is deliberately out of scope: it holds no cache of
received values — each exchange decodes into a fresh record — so no edge can
filter against a stale value and there is nothing for a second path to bypass.

Suppression belongs on the sending side, and that is not a preference. The
sent-state shadow retires from the wire's own section byte, so a section an
answer does not carry stays due and crosses on the next standing answer, one
poll period later. Dropping the section at the receiver instead would advance
the shadow against a value that never arrived, which is permanent loss.

> **REFUSED:** delete core1's receive-side edge filter.
> **WHY:** an unchanged section crosses on consecutive polls — 1000/s in
> DUAL-HOST — so the filter is what keeps core0 off the poll rate.
> **REOPENS:** a carrier that advertises only on change at the sender, in
> every relation.

> **REFUSED:** drop the section decode at the receiver instead of suppressing
> at the sender.
> **WHY:** the shadow advances on the responder's own send, so a receiver-side
> drop is permanent loss rather than a deferral.
> **REOPENS:** never, while the shadow retires from the wire's section byte.

> **REFUSED:** keep the second arrival path and fold its value back into
> core1's receive-side cache on every arrival, rather than deleting the path.
> **WHY:** four mechanisms added to make a second delivery path safe, against
> deleting the path, and it opens a new loss window.
> **REOPENS:** a second arrival path that is required rather than incidental.

## Replacement Storage Route

The exact protocol is `era_host_peer_storage_contract.md`, which is also
canonical for which relations admit the lane and in which role. The route rules
below are the same in every one of them.

- Storage route selection and enqueue run only from the cold core0 task
  boundary. Matrix-scan transport hooks consume only the cached storage-active
  suppression token; they do not scan EEPROM, calculate CRC, compare sources,
  enqueue storage, or apply storage.
- The initiator may select storage only in a confirmed relation that admits it
  as initiator, with local requested policy enabled, both session endpoints
  advertising the required bulk-page support, no mandatory status revalidation
  due, no CLEAN quarantine on this half, and dedicated core1 result capacity
  available.
- The responder never selects a storage route. In a confirmed relation that
  admits it as responder, it may publish an immutable storage responder
  snapshot and admit only the matching initiator request in that request's
  response slot, provided CLEAN quarantine is not set.
- The unpinned `BUSY`/core0 pin handoff remains nonexclusive and keeps normal
  HOST matrix response admission open, for the pull probe and the push open
  alike. Once a validated `TRANSFER` starts in either direction, storage takes
  everything an initiator originates, in one clamp: the matrix source-push owner
  route, and the standing exchange's cadence through the grant's enable bit. The
  clamp is a route-due mask *down to* `ATTACH_STATUS` rather than a list of bits
  to clear, which keeps it covering exactly what it covers as route bits retire.
  **It lifts at transfer-verified**: the apply phase runs with the owner routes
  and the cadence restored, so the initiator's keys and runtime sections cross
  between the apply's EEPROM operations. `SESSION_STATUS` recovery/revalidation
  still preempts, and cancellation follows any authority, relation, policy,
  owner, or source-revision change.
- **Exclusivity suppresses owner routes and never a responder's answer** —
  Common Route Priority above is canonical for why. What it suppresses on the
  responder side is the *content*: the response plan is empty for the transfer,
  so the answer is the section-less ACK and no runtime section crosses
  (`era_wire_contract.md`). Matrix admission is the one exception, and it is a
  payload rule rather than an answering one — the exclusive close flushes the
  matrix relation on both roles, so rows accepted mid-transfer are rows the
  close is about to discard.
- The arbitration exchanges (the `SYNC_STATUS` summary, the per-conflict counter
  form) are nonexclusive proof-class episodes at the storage retry cadence: they
  pin nothing, suppress nothing, and precede the per-domain episodes whose
  direction they decide.
- **Initiation is change-triggered, never periodic.** The lane opens a summary
  exchange on exactly three things: a relation (re)establishment, this half's
  own settled capture while in relation, and a responder-changed signal. Nothing
  else, and no cadence — a DUAL-HOST window with no settled config change
  carries no storage transaction at all (`era_invariants.md`).
- The responder-changed carrier is **one section in both relations**: the HSRSP
  storage news section, riding each relation's own lane. It names nothing and
  arms the same summary a local settled capture arms, so the three triggers are
  three producers of one path (`era_host_peer_storage_contract.md`).
- **The hint is consumed as news, never as a level**, and that is a route
  property rather than a storage detail: this consumer queues an exchange, so a
  value re-delivered by a repeating carrier arms one episode per delivery — the
  polling shape this lane exists to avoid, reached without any route becoming
  periodic. Probe scheduling is audit/hint driven at the cadences above, and a
  hint cannot bypass quiet deadlines, the `BUSY` pin handoff, generation
  allocation, or route priority.
- The unpinned handoff cannot bypass a dirty source deadline: a superseded
  proof reschedules its domain after the trailing quiet interval. Source
  supersession alone does not force `SESSION_STATUS`, an exclusive transfer
  still performs normal matrix recovery, and a CHUNK-stage supersession
  terminates in its admitted slot rather than by response timeout
  (`era_host_peer_storage_contract.md`).
- After a push completes, the responder's identity rotation
  (durable-declare-then-rotate) drops the wire for a few milliseconds and the
  initiator returns through `SESSION_STATUS` revalidation into a fresh relation
  open — the reopen arbitration re-proving the pair is the push lane's accepted
  close cost.
- Close or abort after an exclusive transfer flushes the HOST-PEER matrix
  relation on both roles. PEER then has a forced source-push baseline; after
  mandatory status, that baseline is the first normal route before response
  polling or heartbeat. A proof-only `MATCH` closes without flushing the live
  projection.
- **There is one storage lane, not one per relation.** Policy, generation,
  authority, state and diagnostics belong to the lane, and a relation change
  rotates them. The engine is relation-independent, so admitting a further
  relation onto it needs neither a second lane nor a second route kind.
- The route is a dedicated cold-task storage lane, not a new general owner-route
  kind. The initiator publishes one immutable request to core1 when due; the
  responder publishes a pinned immutable responder snapshot. The normal route
  mask admits only mandatory `SESSION_STATUS` while storage is exclusive.

## SESSION_STATUS Discovery And Liveness

`SESSION_STATUS` is the discovery/revalidation route and nothing else. It
carries no advisory bit for another lane: the one it used to carry is retired
un-reused (`era_identifier_map.md`), and the fact it carried rides the response
section named in `era_wire_contract.md`. Carrying such a bit is what kept the
frame running on a cadence inside a serviced relation, which is the property
that failed.

- No sync policy gates it: discovery, role changes and recovery are always
  reachable, in every relation.
- Peer-unknown bootstrap, local authority changes, policy/generation changes
  and stale recovery may use immediate or fast `SESSION_STATUS` revalidation.
- Known-relation idle liveness does not use `SESSION_STATUS` probing at all:
  the relation's own lane is what refreshes it.
- Peer-unknown discovery probes at the 25 ms
  `ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS` and backs off to
  `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS` (500 ms) after
  `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER` (10) consecutive misses: a half
  with no confirmed peer must not probe at the bootstrap period forever.
- Accepted relation traffic MUST refresh liveness **regardless of class** —
  storage probe/chunk/apply/complete exchanges refresh the same window as
  heartbeat, source-push and the runtime poll (`era_invariants.md`) — and MUST
  avoid a duplicate idle `SESSION_STATUS` probe inside that window.
- **Every half opens the wire at Low; a peer-unknown responder with no
  serviced relation still steps toward a rate it hears, and the peer-unknown
  initiator never moves off *running*.** Boot Low is the common path
  (`era_split_transport_scheduler_start_communication_core()` in
  `era_split_transport_scheduler.c`). The listener ring is the recovery for a
  pair already at two running rates, and it hangs on the discovery probe
  above, not on a mechanism of its own. The agreed restart owns the raise
  after the relation opens, not this recovery.

  It exists because a two-phase agreement cannot be the only way the rate
  changes: two halves that end up at different levels could never open a
  session, so the rate could never be changed back. What resolves it is that
  the two roles the planner already assigns to a peer-unknown pair
  (`split/era_split_mode_planner.c`: Left initiates, Right responds) are
  exactly a talker and a listener, and a listener can hear what a talker
  cannot: whether anyone is there. The talker probes at its running level and
  does nothing on silence. The listener reads the responder's own accepted and
  undecodable counters over a `ERA_SPLIT_LINK_SCAN_DWELL_MS` dwell; noise with
  no decoded frame in the dwell is a talker at a rate it is not listening at,
  and it steps to the next level on the closed ring High → Medium → Low →
  High. Silence leaves it in place, one decoded frame cancels the step, and a
  peer that has just been given power over the cable holds the line low until
  its firmware claims the pin and produces exactly one undecodable arrival,
  which is why the threshold is two. **The cable carries power from the hosted
  half**, so a peer is never connected and unpowered for longer than its own
  boot: plugging the cable is what boots it, and every plug is therefore a
  late-peer case for the listener. The rule, its constants and every
  convergence case are canonical in `split/era_split_link.h`'s
  **Reconciliation**.

  Why it is a route-layer fact: the three terms the listener needs are this
  layer's — the settled wire role, ANDed with wire availability so an
  unclassified or capped wire is neither role, whether the plan is the
  peer-unknown bootstrap at all (a responder beside a known peer has nothing to
  listen for, and folding that in is what makes "the talker never moves"
  structural: a Left half is a responder with no relation only beside a peer
  whose status is neither, and it is not a listener there), and whether a
  relation is serviced — and the step itself is performed in
  `era_split_transport_scheduler.c`'s own backend teardown-and-rebuild window,
  the same one the storage rotation and the diagnostics flush take, because a
  divider change needs the backend owner torn down and it fires at the one
  moment that is trivially safe: there is no relation to interrupt. The
  scheduler also holds the assert that the dwell outlasts two backed-off probes
  with their slowest response windows, because it is the one unit that includes
  all three constants.

  What this replaced was a drop to High after a fixed silence, on the reasoning
  that High is the level every image knows so two disagreeing halves would meet
  there. They did — and so did a working pair whose halves had powered a few
  seconds apart, and a pair on a cable that could hold only Low. Silence is
  what an absent peer, a peer still booting on the power the cable just gave
  it, and a peer at another rate all sound like from the talking side; the
  rule that moved on it could not tell them
  apart, and the reconciliation built on it then recorded the working level as
  failed on both halves. The refusal to keep a silence floor beside the step is
  written in the header.

- **A serviced relation raises to the winner's stored level at a shared-clock
  deadline, and a half that is then running a level it did not store adopts
  it.** The rule is canonical in `split/era_split_link.h`'s **Reconciliation**;
  what belongs here is why it is a route-layer fact and why the winner is
  DUAL-HOST Left / HOST-PEER HOST.

  The route layer supplies the evidence and the window. *The relation opened*
  means `SESSION_STATUS` has confirmed both halves at Low. The standing
  exchange then carries the time-anchor, which is what puts T_commit in one
  domain; the initiator does not arm the link act until one has been applied
  **in the current relation**. Relation rotation invalidates that adoption fact
  without clearing the already corrected shared-clock reading. This distinction
  is load-bearing when halves reboot at different times: an anchor accepted from
  the old responder uptime cannot authorize a deadline against the newly
  rebooted responder's epoch.
  The raise itself is `era_split_transport_scheduler_apply_link_level()` in
  `era_split_transport_scheduler.c` — the same owner-down / `set_speed` /
  serial-recover window the listener's step uses, fired on both halves at the
  agreed deadline, relation identity kept so the 100 ms silence watch is the
  window they must fit. `wire_scale()` follows that `set_speed` in
  `era_split_transaction_backend_rp2040.c`; DUAL-HOST's poll is the one
  period that reads it. A raise that then fails reverts *running* to Low for
  the rest of the session and writes nothing — the retired silence fallback
  in other clothes would have stored the failure.

  The first EEPROM SYNC is not part of that surface. The storage initiator
  holds the relation-open audit until `era_split_link_runtime_settled()`
  (`era_host_peer_storage.c`), so a content-moving episode is not torn by the
  raise and a converged pair does not light the red lamp at Low. The link
  byte still does not enter the SYNC engine.

  The winner identity is a projection of the settled mode this layer already
  holds — DUAL-HOST Left, HOST-PEER HOST — plus `is_left`. It is not a second
  planner. The winner's stored byte is the link act's param; that is the UX
  result of HOST-wins / Left-wins, not a copy of the storage engine's
  latest-change rule. **Left remains the one arbitrary-tie answer this
  firmware gives** wherever the two halves disagree and no USB-host fact
  assigns a winner (`era_split_mode_planner.c`, `era_host_peer_response.c`,
  `era_host_peer_storage_contract.md`'s **Arbitration**,
  `split/era_split_restart_agreement.h`). HOST-PEER has that USB-host fact,
  so the pair's target is HOST.

## HOST-PEER PEER Priority

Core0 selects three things in this relation, in this order — the revalidation
frame that outranks everything, the storage lane, and this relation's matrix:

1. Pending `SESSION_STATUS` bootstrap, policy-generation, or recovery
   revalidation.
2. Active HOST-PEER storage step in the storage lane.
3. `HOST_PEER_SOURCE_PUSH` when PEER matrix dirty or forced baseline exists.

The ordering is the transaction-boundary rule above: an already-published
storage request finishes without mid-frame cancellation, and a status becoming
due during it prevents the next storage publication rather than aging in the
generic queue.

**The standing exchange is not a fourth entry, and that is why it is not on the
list.** It is not selected against these at all: core1 reaches its standing
service only on a pass where the request queue and the storage lane both did
nothing, so route priority is expressed there as position in that pass rather
than as a core0 predicate declining to select. The matrix source-push preempts
it through the same order, which keeps the relation's key input ahead of its
runtime sections without either side arbitrating.

Healthy HOST-PEER traffic MUST suppress the duplicate periodic `SESSION_STATUS`
refresh loop, but MUST NOT suppress discovery, liveness or pending
revalidation.

## HOST-PEER HOST

- Does not select independent routes, and responds only to admitted PEER
  requests.
- Accepted heartbeat/source-push refreshes relation liveness; source-push with
  matrix updates the HOST-owned peer matrix cache.
- When no HOST-owned section is open or due, the response remains the existing
  one-byte `HOST_PEER_ACK_STATUS`.
- `HOST_PEER_HOST_SOURCE_RSP` may be admitted only in the same response slot, as
  an alternate response to a PEER heartbeat or source-push request, and MUST NOT
  be selected as an owner route or sent outside an admitted PEER request's
  response window. Which sections it may carry is the eligibility table's
  question (`era_wire_contract.md`); it carries them when they are dirty or
  forced by a relation flush.
- The PEER's standing exchange makes its heartbeat requests for the
  `RUNTIME_RESPONSE_POLL` reason. The HOST responder sees an ordinary heartbeat
  request and chooses ACK or HSRSP from HOST-local dirty state — **it cannot
  tell which core timed the request and does not need to**, which is what lets
  the initiator side change without touching this one.
- **A busy HOST answers that slot**: its heartbeat admission carries no
  storage-exclusivity term, and the suppression falls on the response plan
  instead, so the answer is the section-less ACK.

## DUAL-HOST

The persisted sync requested values and the initiator/responder ownership table
that assigns Left and Right are canonical in `era_authority_contract.md`. What
follows is only the route consequence.

- DUAL-HOST matrix, digest and mirror routes stay closed. There is no matrix to
  forward with both halves reporting directly, and that is the reason rather
  than a scheduling one.
- DUAL-HOST storage is open and runs the Replacement Storage Route above with
  Left as initiator and Right as responder.
- **DUAL-HOST runtime is open**, with the same ownership: the Left's grant
  carries `RUNTIME_SECTION_PUSH` and `RUNTIME_RESPONSE_POLL`, the Right grants
  nothing and answers only in admitted slots. Which runtime *sections* the
  relation carries is a separate question from whether the route is open, and
  the eligibility table answers it (`era_wire_contract.md`).
- **Every runtime section this relation carries rides the standing exchange,
  defines no new service reason and adds no new cadence** — the INPUT layer,
  AUTHORITY, RGB, the time anchor, ACTIVITY, the visual pressed-baseline
  family, and the storage-pending bit alike. They are edge-armed and latest-state, crossing under
  `RUNTIME_SECTION_PUSH` outbound and `RUNTIME_RESPONSE_POLL` inbound, with
  their effects policy-gated where they carry a policy bit
  (`era_authority_contract.md`). Three consequences that are not free-standing
  preferences: nothing gates the poll rate, which stays unconditional, while a
  local section going dirty is due immediately rather than at a rate; ACTIVITY's
  window derivation from the off-by-default tapping options is what keeps a
  fresh-defaults image from ever putting that section on the wire; and the Right
  is the relation's time authority, whose anchor yields to everything.
- The visual pressed-baseline family is open in **both** cells, so the reactive
  half crosses with it, under the RGB policy bit's sender and receiver arms
  (owner record 2026-08-09: RGB SYNC targets complete synchronization, behaving
  as HOST-PEER does).
- Producer paths: the Left marks the publication route-due bit for the plan
  fields a scan can move — the INPUT layer edge, the advertised-activity change
  (window-bounded, so fresh defaults never fire it) and the visual baseline
  edge. The Right selects no route, so its changes ride its snapshot rebuild at
  housekeeping cadence.
- The standing plan's RGB and ACTIVITY fields follow the
  plan-field-follows-eligibility rule above, so a HOST-PEER plan never fills
  them.

## Stale Recovery

- The responder stale watch MUST count every frame the wire transaction engine
  accepts, regardless of traffic class, and has no exemptions. A durable apply
  earns no silence grace: what feeds the peer's watch through one is core1's
  standing liveness beat, from the side that is still running
  (`era_host_peer_storage_contract.md`).
- **The watch's window is canonical here**: `ERA_SPLIT_RESPONDER_SILENCE_MS`, a
  constant 100 ms, the same on both halves and standing on its own rather than
  derived from any beat. It is what the poll periods are held against — a
  responder is fed by the relation's own traffic, so the limit must sit
  comfortably above whichever period that relation polls at — and two
  `_Static_assert`s hold both poll periods to half of it.
  `ERA_SPLIT_SESSION_STALE_MS` defaults to 0, which selects the constant; a
  build that sets it nonzero fixes the window instead
  (`scheduler/era_split_transport_scheduler_timing.c`, `relation_stale_ms()`).
  Every "100 ms responder-silence limit" elsewhere in the active set is an
  application of this rule, not a second constant.
- Local HOST close invalidates stale both-hosted peer-session facts for
  planning. Recovery MUST return through peer-unknown `SESSION_STATUS`
  bootstrap instead of allowing both halves to become initiators from stale
  session facts.
- Payload validation failure or missed response closes the fast path and
  returns to `SESSION_STATUS` bootstrap/backoff.
- Route failure sites must mark stale or revalidation dirty/due state and leave
  recovery IO to the scheduler consumer.
- Cable removal must converge to relation unknown and peer cache invalid.

## Due/Deadline Model

- The transport scheduler is event-driven plus deadline/due, not pure
  interrupt-only. Positive events mark dirty/due state; absence and liveness
  are deadline decisions. Missing events, responder silence and timeout
  decisions are deadline/due conditions, not producer events.
- **A dead core1 is detected on both wire roles, by one detector with two
  co-dependent sensors**, both riding existing housekeeping passes and reading
  one core1-published word each, not a core0 request to core1. The responder's
  word is wire-anchored accepted RX; the initiator's is completed Core1
  lifecycle-loop passes. No new poll, no wire traffic and no new core1 writer
  are introduced.

  - The receiving half's sensor is the **responder-silence watch** above: it has
    arriving frames to miss.
  - The initiating half receives nothing to miss, so its sensor is that watch's
    structural twin, the **initiator silence watch**
    (`scheduler/era_split_transport_scheduler_timing.c`), whose progress signal
    is the communication-core lifecycle's completed-loop count through
    `era_split_communication_core_progress_count()`
    (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`).
    Queue, storage and standing service all return through the same increment,
    and an idle loop increments immediately before parking. No legal service
    occupancy may therefore hold that word unchanged for the unresponsive
    bound; the scale-specific device gate is the proof where a runtime wire
    level prevents one compile-time expression from being sufficient. A
    genuinely stopped core advances none of them. The watch arms only where
    service is supposed to be happening:
    initiator role, live
    `CORE1` lease, known peer, no stop report, and a **granted** standing plan,
    cached at the publish from core1's own acceptance test
    (`relation_generation != 0`) so the watch cannot drift from the plan
    builder's withhold set. That last gate is load-bearing, not defensive:
    ungated, the watch fires every bound on a healthy powered-without-USB pair,
    where the plan is legitimately withheld and no exchange is owed.
  - The **in-flight expiry** (`era_split_transport_scheduler.c`, beside the
    cancel gate it feeds): a pending initiator request that core1 has neither
    consumed nor expired for the same bound, with no published result — a ready
    result is the opposite evidence, core1 answered and only core0's drain is
    late, as across a sliced durable apply. It marks wire-role dirty so the
    adjacent dirty-and-pending cancel fires the same pass, and latches
    peer-stale so the replan survives the cancel's bounded early returns.

  The bound for both is `ERA_SPLIT_CORE1_UNRESPONSIVE_MS`, defined as
  `ERA_SPLIT_RESPONDER_SILENCE_MS` rather than as a new number: the two watches
  ask the same question from the two ends of the wire, so every cadence already
  asserted against the silence limit is inside this one too. It takes the
  constant and never the `ERA_SPLIT_SESSION_STALE_MS` override, because it
  measures core1 responsiveness and no build configuration may widen that. A
  `_Static_assert` holds twice the standing liveness beat inside it, at equality
  for today's values, so widening the beat fails the build loudly.

  **The convergence is two-stage by design, and the stages are the two
  sensors.** A silence fire replans the mode (`LOCAL_NO_LINK`, session forget),
  but the fast path re-blesses the dead lease, so the judgment — declare-dead,
  relaunch and its cap, wire-unavailable — arrives through the expiry on the
  first post-replan bootstrap probe. Neither sensor alone suffices: the silence
  watch without the expiry recreates the same rot one mode later. Both feed the
  ordinary sticky peer-stale/revalidation path, and the judgment belongs to the
  owner layer, never to an inline recovery at the detection site.
- Responder RX may block waiting for incoming serial data, but role/reset,
  admission and quiesce state changes MUST have an explicit wake or receive
  cancel path.
- On RP2040 core1, responder-idle wait authority is the PIO RX FIFO plus
  error/deadline/cancel/owner-epoch predicates. IRQ, `SEV` and `WFE` are wake
  mechanisms only, and the bare core1 path must not use ChibiOS thread wait or
  resume APIs.
- HOST-PEER PEER matrix dirty may mark `HOST_PEER_SOURCE_PUSH` route due
  immediately without opening full planning.
- **No runtime route deadline sits in the core0 deadline set in either serviced
  relation**, and its absence is the measurable half of the grant: core0 does
  not wake on a poll period at all, so a shorter period costs it nothing. Core1
  holds one period and one liveness deadline under the grant, and the two
  runtime reasons are what it stamps rather than what core0 schedules.
- **The scan-rate task reads the clock only once it has a use for the answer,
  and the raw pre-filter is never the authority.** The housekeeping gate is two
  halves. The event arms are latches and published words and need no time at
  all; the deadline arm compares the raw hardware microsecond counter against a
  stamp published beside the millisecond deadline
  (`scheduler/era_split_transport_scheduler_timing.c`). The stamp is built to
  land early rather than late — up to a millisecond before the deadline's own
  millisecond begins, and never after it — so an open pre-filter still owes
  `timer_expired32()` against `next_scheduler_deadline_ms`, and every cadence
  fires in the millisecond it fired in before. **That last clause is measured,
  not argued, and it is re-taken rather than quoted**: read `wire maint entry=`
  on an idle no-cable half, where every maintenance column is flat, and its
  rate is the 10 ms authority poll to within the ±1 ms resolution of the two
  timestamps it is derived from (`era_capture_reading.md` for how a rate is
  read off a counter and a `last` stamp). What makes the split worth its
  two functions is the entry, not the test: `timer_read32()` costs about
  0.42 µs even answering from the millisecond cache
  (`era_qmk_fork_ledger.md`), and this one runs at the matrix scan rate.

  > **REFUSED:** let the raw pre-filter decide outright and drop the
  > millisecond compare.
  > **WHY:** the stamp locates the deadline's millisecond only to within one
  > millisecond, so deciding on it alone runs every cadence up to a millisecond
  > early — and compounds, because each early sample re-arms the next deadline
  > from its own early reading.
  > **REOPENS:** a core0 deadline shorter than a millisecond, which the
  > millisecond grid cannot express at all. Precision alone does not reopen it:
  > an exact stamp is worth 0.021 µs a pass, five per cent of one clock read.

  > **REFUSED:** inline the gate's event-arm accessors into a header, since each
  > compiles to 12–24 bytes behind a full `bl`.
  > **WHY:** they read state declared only in
  > `communication_core/era_split_communication_core_internal.h`, so inlining
  > publishes the core's internals to the scheduler — and two of them carry a
  > `__DMB()` that stays either way, so what is bought is a call frame and what
  > is sold is the encapsulation boundary.
  > **REOPENS:** a published accessor surface the core owns deliberately, rather
  > than the scheduler reaching through the internal header.

  **There is one clock, and knowing that is what bounds the pre-filter's
  error.** `CH_CFG_ST_FREQUENCY` is 1,000,000 with a 32-bit tickless counter and
  `st_lld_get_counter()` returns `TIMER->TIMERAWL` — the register
  `timer_hw->timerawl` reads — so `timer_read32()`
  (`platforms/chibios/timer.c`) is that same counter divided
  by a thousand past an offset, and the millisecond cache exists to do the
  division once per millisecond rather than once per call
  (`era_qmk_fork_ledger.md`). The pre-filter is therefore not a bridge between
  two clocks that could drift; it is the raw unit compared against a grid
  derived from it, and the only thing the raw value does not carry is where that
  grid's boundary falls. Three designs would carry it — reading the cache's own
  window start, learning the grid once at boot from `ticks_offset mod 1000`
  (constant, because the counter is one register), or moving core0's deadline
  set onto raw microseconds as core1's already is. **Each buys the same
  0.021 µs**, so the choice among them is not a performance question, and the
  third is the one that makes the question stop existing.

### Latch Concurrency Rules

Dirty/due producers run in several contexts (main-loop housekeeping, matrix
transport hooks, VIA command handling, suspend/wakeup hooks, USB device-state
notification), so the latch discipline is part of the contract:

- Producers set latches OR-only, under `ATOMIC_BLOCK_RESTORESTATE`. A producer
  never clears a latch and never calls scheduler planning.
- The scheduler consumer clears by atomic read-and-clear or consumed-bit clear,
  so a producer event arriving during planning is not lost. Clearing all flags
  after planning drops any event raised mid-plan.
- Consume a latch as an edge, not a level, whenever the producer's clear and the
  consumer's clear are separate events; otherwise the window between them
  re-arms work that was already taken. The storage hint is this rule's longest
  case: it was a level whose producer cleared per domain at convergence while
  its consumer cleared per episode, and every compensation for that gap was a
  patch on the mismatch rather than on the level. It is a forward-only news
  value now (`era_host_peer_storage_contract.md`). **The generalizable form:
  when a level's clear is derived from something the consumer also observes,
  prefer a value the producer can only move forward.**
- Do not mark a latch from raw USB/SOF events before the reducer commits, or
  from a policy setter before the new generation commits.
- Scheduler init seeds a full snapshot, or treats unknown cached
  authority/session/policy state as dirty, so pre-init events are not lost.
