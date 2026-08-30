# ERA Route Contract

Genre: contract
Canonical for: route ownership and priority, route kind/reason, all service
cadences and poll period values, the standing exchange grant, storage route
admission and exclusivity, freshness handoff, stale recovery, the due/deadline
model, and latch discipline

Wire markers, bodies and eligibility: `era_wire_contract.md`. Storage protocol
and news: `era_host_peer_storage_contract.md`. Authority sample and
`matrix_ready`: `era_authority_contract.md`. Link ring cases:
`split/era_split_link.h` **Reconciliation**.

## Common Route Priority

1. Required attach/status bootstrap, policy, or recovery revalidation when due.
2. Active exclusive storage route while its relation remains valid.
3. Mode-specific normal route.
4. No-op.

Storage is exclusive against normal matrix/heartbeat/HOST-source-poll traffic,
not against mandatory relation recovery.

**Priority applies at transaction boundaries; an exchange already begun is
never preempted.** Core0 admits only one of the generic request ring and the
dedicated storage request slot at a time.
`era_split_transport_scheduler_initiator_route_available()` in
`split/scheduler/era_split_transport_scheduler_routes.c` refuses a generic
publication while `era_host_peer_storage_initiator_request_pending()` in
`split/era_host_peer_storage.c` is true. Storage submit refuses while
`general_initiator_pending` or `status_revalidation_due` is set
(`split/era_host_peer_storage.c`).
`era_split_transport_scheduler_refresh_route_due_flags()` in
`split/scheduler/era_split_transport_scheduler_timing.c` and the scan-path
initiator step in `split/era_split_transport_scheduler.c` both zero those
generic due bits while a storage request is pending, and otherwise mask them
to `ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS` while
`era_host_peer_storage_route_exclusive()` in `split/era_host_peer_storage.c`
is true. Due facts stay armed.

**A Core1 request's queue freshness begins at shared publication, not request
construction.** Window = `ERA_SPLIT_PEER_RESPONSE_WINDOW_MS` 20 ×
`era_split_transaction_backend_wire_scale()` (`split/era_split_transaction_backend_rp2040.c`)
+ `ERA_SPLIT_COMMUNICATION_CORE_REQUEST_HANDOFF_MARGIN_US` 5000:
25 / 45 / 85 ms at High / Medium / Low.
`era_split_transport_scheduler_core1_request_queue_window_us()` in
`split/scheduler/era_split_transport_scheduler_routes.c` stamps it. Low-level
maximum must stay below `ERA_SPLIT_CORE1_UNRESPONSIVE_MS` 100 or the build
fails. Queue-residence only; the selected request's own window starts after
BEGIN.

**The exclusive span is the transfer phase, not the episode** — validated
`TRANSFER` through each role's transfer-verified boundary and an abort's
bounded cleanup. Apply is local flash. One narrower gate outlives exclusivity
on a push initiator: `era_host_peer_storage_standing_suppressed()` in
`split/era_host_peer_storage.c` is route-exclusive OR (initiator AND
(`PEER_PUSH_APPLY` or `PEER_PUSH_COMPLETE`)). Composition:
`split/era_host_peer_storage_standing_policy.h`. That gate clears the standing
plan `enabled` bit while storage APPLY/COMPLETE control remains admitted.
Route admission outside the standing cadence is unchanged.

CLEAN quarantine is stronger than transfer exclusivity.
`era_split_restart_agreement_storage_quarantined()` in
`split/era_split_restart_agreement.c` is true from selection before CLEAN
PREPARED through COMMIT and controlled reset. It forbids
select/publish/admit/answer/snapshot of storage, still admits
`SESSION_STATUS` and the standing exchange, and clears only on controlled
reset. Why the restart is required:
`era_host_peer_storage_contract.md` **Why An EEPROM Clean Is An Agreed Restart**.

**Exclusivity suppresses the routes an initiator selects and never a
responder's answer.** The responder answers the admitted slot under
exclusivity with no section
(`split/scheduler/era_split_transport_scheduler_responder.c`).

Heartbeat admission is the relation fact (confirmed HOST-PEER HOST with a
session that admits its PEER's matrix, or confirmed DUAL-HOST Right) and
carries no exclusivity term. Matrix admission is that HOST-PEER fact AND
`!era_host_peer_storage_route_exclusive()`. Deriving the first from the
second makes a busy HOST refuse the section-less ACK; the standing liveness
beat then meets that refusal on every frame and collapses the pair.

## Route Kind And Reason

Route kind is the selected wire operation. Route reason is the initiator-local
service cause. The responder sees only the admitted wire operation and its own
dirty state. Enums: `split/era_split_wire_router.h`.

| Reason | Wire op | Trigger | C enumerator |
| --- | --- | --- | --- |
| `ATTACH_STATUS_REVALIDATION` | `SESSION_STATUS` | bootstrap, policy, discovery, recovery | yes |
| `HOST_PEER_MATRIX_SOURCE_PUSH` | `HOST_PEER_SOURCE_PUSH` | PEER matrix event or forced baseline; no cadence | yes |
| `HOST_PEER_STORAGE_IDLE_PROOF` | storage probe | relation-open audit or responder-changed hint, including nonexclusive `BUSY`/pin retry; no periodic patrol | none (`era_identifier_map.md`) |
| `HOST_PEER_STORAGE_ACTIVE` | chunk/apply/complete or abort cleanup | after validated `TRANSFER` | none (`era_identifier_map.md`) |
| `RUNTIME_SECTION_PUSH` | `HOST_PEER_SOURCE_PUSH` with a runtime mask | local runtime section differs from last confirmed | yes |
| `RUNTIME_RESPONSE_POLL` | `HOST_PEER_HEARTBEAT` | initiator opens a slot so the responder can speak | yes |

`era_split_wire_router_select_owner()` in `split/era_split_wire_router.c`
selects only `ATTACH_STATUS` then HOST-PEER PEER matrix source-push.
**Core0 selects neither runtime reason, in either relation.** Both belong to
core1's standing exchange, which stamps `ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT`
so an `rk`/`rr` pair keeps its meaning (`split/era_split_wire_router.h`).
Storage is a dedicated cold-task lane, not an owner route.

> **REFUSED:** add `HOST_PEER_STORAGE_*` as `era_split_route_reason_t` values.
> **WHY:** that would put the dedicated storage lane through the generic owner ring.
> **REOPENS:** storage traffic that must share the generic initiator ring.

| Cadence | Value | Rule |
| --- | --- | --- |
| `SESSION_STATUS` post-relation | none | no post-relation cadence and no in-relation edge. Edges that still raise revalidation: `era_authority_contract.md` |
| `ERA_SPLIT_SESSION_REFRESH_PERIOD_MS` | 50 | known peer, while that relation's own lane is not live (`scheduler/era_split_transport_scheduler_timing.c`) |
| `ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS` | 25 | peer-unknown discovery until `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER` 10 consecutive misses |
| `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS` | 500 | peer-unknown discovery after that streak |
| `ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS` | 2 | `SESSION_STATUS` response window while the peer is unknown; known-peer uses `ERA_SPLIT_PEER_RESPONSE_WINDOW_MS` 20 |
| core0-originated route cadence in a live relation | none | **Nothing core0 selects runs on a cadence in either live relation.** HOST-PEER's PEER keeps one core0-selected route (matrix source-push, event-driven). Section set: `era_wire_contract.md` |
| `ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS` | 10 | HOST-PEER standing period. Does not scale with link. No runtime route deadline in the core0 set, so shortening costs core0 no wake |
| `ERA_HOST_PEER_RGB_STATE_SNAPSHOT_PUBLISH_PERIOD_MS` | 10 | defined as the HOST-PEER standing period. Arms HOST-PEER HOST and DUAL-HOST Right responder RGB snapshot republish. Core0 deadline set includes this deadline only in HOST-PEER HOST (`scheduler/era_split_transport_scheduler_timing.c`); DUAL-HOST Right rides the authority-sample wake at the same 10 ms |
| `ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS` | 10 | `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10. The architecture's one poll; sample itself: `era_authority_contract.md` |
| `ERA_SPLIT_PEER_RESPONSE_WINDOW_MS` | 20 | **not a cadence.** A non-OK standing exchange latches `stopped` and core1 originates nothing until core0 republishes the plan (`split/communication_core/era_split_communication_core_standing.c`). Deadline re-armed from the poll decision, not completion |
| `ERA_SPLIT_DUAL_RUNTIME_POLL_MS` | 1 | **one period, run unconditionally.** No activity window, quiet rate, or hint. A local dirty section is due immediately. **This period is the responder's latency bound.** 1 / 2 / 4 ms High / Medium / Low = × `era_split_transaction_backend_wire_scale()` |
| `ERA_SPLIT_STANDING_LIVENESS_MS` | 50 | `ERA_SPLIT_RESPONDER_SILENCE_MS` 100 / 2. Not per-relation. Fires only while `enabled` is clear |
| `ERA_SPLIT_TIME_ANCHOR_REFRESH_MS` | 60000 | relation time-anchor refresh; the section is latest-state (`era_wire_contract.md`) |
| `ERA_SPLIT_LINK_SCAN_DWELL_MS` | 1500 | listener dwell. Scheduler asserts it outlasts two backed-off probes with their slowest response windows (`split/era_split_transport_scheduler.c`) |
| `ERA_SPLIT_LINK_SCAN_NOISE_MIN` | 2 | undecodable arrivals in one dwell before the listener steps |
| `ERA_SPLIT_LINK_UPGRADE_CONFIRM_MS` | 200 | raise-confirm window; asserted ≥ 2 × `ERA_SPLIT_RESPONDER_SILENCE_MS` 100 |
| `ERA_SPLIT_LINK_UPGRADE_WAIT_MS` | 500 | non-winner wait for a raise that is not coming |
| `ERA_SPLIT_RESTART_ARM_TIMEOUT_MS` | 60 | restart arm timeout (`split/era_split_restart_agreement.h`) |
| `ERA_SPLIT_RESTART_COMMIT_DELAY_MS` | 120 | commit delay; scheduler asserts it sits past the arm timeout plus two HOST-PEER poll periods |
| storage audit / retry | `ERA_HOST_PEER_STORAGE_RETRY_MS` 25 | no periodic idle-proof; probes from the seven-domain audit sweep and the news-armed summary (`era_host_peer_storage_contract.md`) |
| trailing quiet | `ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS` 1000 | interval that gates dirty source capture lives in `era_host_peer_storage_contract.md` |
| episode | `ERA_HOST_PEER_STORAGE_EPISODE_MS` 5000 | 5 s |
| failure streak | `ERA_HOST_PEER_STORAGE_MAX_FAILURES` 40 | `ERA_HOST_PEER_STORAGE_PEER_SILENCE_MS` 1000 / `ERA_HOST_PEER_STORAGE_RETRY_MS` 25; silence is 2 × `ERA_HOST_PEER_STORAGE_PEER_STALL_WORST_MS` 500 (`split/era_host_peer_storage.h`) |
| probe backoff | shift-capped retry | `ERA_HOST_PEER_STORAGE_RETRY_MS` 25 << streak, streak cap `ERA_HOST_PEER_STORAGE_BACKOFF_MAX_SHIFT` 6, delay cap `ERA_HOST_PEER_STORAGE_BACKOFF_MAX_MS` 1000. Distinct from the quiet deadline; only this is a backoff |

**A new service reason must define its own wire surface, ownership, and
cadence before execution opens for it.** Reusing a lane is not defining one.

**The period is not measured and its bound is the wire, not core0.** At 1 ms
and 460800 a worst-case exchange is ~0.80 ms with 0.2 ms idle; an overrun
re-arms a past deadline and the cadence stops rather than degrades. Every
`_Static_assert` on a poll period is an upper bound against
`ERA_SPLIT_RESPONDER_SILENCE_MS` 100, so a too-short period cannot fail a
build. Sleep fractions: `era_performance_gates.md` **Fixed Baselines**. A
device check watches for a poll rate far above the configured period
(`era_capture_reading.md`).

## Runtime Execution Owner

**Core0 never polls core1.** Core1 publishes; core0 wakes on the edge — a
ready initiator result, a ready responder result, or the standing state's
change sequence (`era_split_transport_scheduler_task()` in
`split/era_split_transport_scheduler.c`). Result carries lane facts; standing
state carries the response section set (see **One carrier for the response
section set**). A request core1 consumed without a result (full result ring)
unsticks on the next authority sample.

While Core0 is unavailable, Core1 may answer repeated section-bearing
HEARTBEATs from the same immutable responder snapshot. Once one successful
result for the exact owner/relation/snapshot/section-mask tuple is pending,
later successful HEARTBEAT replies do not reserve another general-ring slot
(`split/communication_core/era_split_communication_core_responder_result_policy.h`).
SESSION and both push result kinds never coalesce.

Route authority is not execution ownership. Epochs move core1 only.
`CORE1_FULL` and the forbidden core0 executor, lease, wait-adapter and
fallback forms live in `era_invariants.md`.

### The standing exchange grant

**One route kind may run on core1's own period, in every serviced relation.**
The grant covers `RUNTIME_RESPONSE_POLL` and `RUNTIME_SECTION_PUSH` and
nothing else. Built by `era_split_transport_scheduler_build_standing_plan()`
in `split/era_split_transport_scheduler.c`; published by
`era_split_transport_scheduler_publish_standing_plan()` in that file;
executed by `era_split_communication_core_standing_service_once()` in
`split/communication_core/era_split_communication_core_standing.c`.

**The period is the only per-relation term.**

| Relation | Period | Scales with link? |
| --- | --- | --- |
| DUAL-HOST | `ERA_SPLIT_DUAL_RUNTIME_POLL_MS` 1 | yes: 1 / 2 / 4 ms High / Medium / Low = × `era_split_transaction_backend_wire_scale()` (`split/era_split_transport_scheduler.c`). The backend holds the baud and every number derived from it; the link unit answers no scale |
| HOST-PEER | `ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS` 10 | no. 10 ms has room |

**HOST-PEER's arm requires `matrix_ready` in both directions**
(`era_authority_contract.md` **Matrix Ready**): the PEER builds no plan until
its own matrix is ready; the HOST answers a heartbeat only once its session
records this PEER as `matrix_ready`. Reaching it raises a status revalidation
that clears the enable bit until the frame round-trips.

| Plan field | Rule |
| --- | --- |
| owner epoch, relation generation, enable bit, period, eligible push/rsp masks, latest-state section bodies | published **on change, never on a pass or a timer** |
| `liveness_period_ms` | published unconditionally on every granted plan (`ERA_SPLIT_STANDING_LIVENESS_MS` 50) |
| restart phase | PREPARE/COMMIT/idle; nonzero commit deadline is an absolute instant |
| field-follows-eligibility | filled only where this relation's push eligibility carries it. `INPUT_LAYER` is the case: HOST-PEER does not carry it |
| publication route-due bit | DUAL-HOST Left only (`ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH`). `era_split_transport_scheduler_scan_idle()` in `split/era_split_transport_scheduler.c` reads the route-due word. Consumed at the top of `era_split_transport_scheduler_publish_standing_plan()` in that file, before the plan is built. `era_split_transport_scheduler_refresh_route_due_flags()` in `scheduler/era_split_transport_scheduler_timing.c` is the backstop, not the consumer |

Core1 accepts a plan only when `owner_epoch` matches, `relation_generation !=
0`, `plan_generation != 0`, and `poll_period_ms != 0`. **The enable bit stops
the cadence and does not stop liveness.** While it is clear but those
identities hold, core1 still runs one section-less exchange after
`ERA_SPLIT_STANDING_LIVENESS_MS` 50 of wire quiet. Rotation bumps generation;
storage standing suppression or a pending `SESSION_STATUS` clears enable; a
wire-role change moves the epoch.

**The result is latest-state, not a queue.** Core1 overwrites it
(`split/communication_core/era_split_communication_core_standing.c`,
`split/era_split_transport_scheduler.c`). It carries the initiator's last
successfully sent `STORAGE_PENDING` as a local confirmation edge.

**Publication uses the responder snapshot's discipline**: odd/even publish
sequence with a claim word.

> **REFUSED:** publish the response plan inside the responder snapshot
> instead of threading it.
> **WHY:** it lowers and re-raises the same table across the core boundary.
> **REOPENS:** a snapshot that already carries the plan for another reason.

A failed standing exchange latches `stopped` against that `plan_generation`.
Core1 never retries; failure is core0's. **A stop raises revalidation and
does not declare the peer stale**
(`era_split_transport_scheduler_apply_standing_state()` in
`split/era_split_transport_scheduler.c`). A stop inside a durable Apply is a
relation failure, not a persistence rollback.

**Core1 still reads no live QMK state.** Core0 publishes the section bodies;
core1 serializes them. HOST-PEER: PEER core1 sends the request; HOST core1
may send only its admitted ACK/HSRSP. Peer-unknown discovery remains
Left-initiated `SESSION_STATUS`.

#### One carrier for the response section set

**A response section rides the standing exchange's answer and nothing else.**
A request from the initiator's core0 lane is answered with the bare control
ACK and carries no section
(`era_split_transport_scheduler_apply_core1_host_peer_result()` in
`split/scheduler/era_split_transport_scheduler_routes.c`). Eligibility is
untouched: it narrows *when* a section crosses, never *whether* the relation
may carry it (`era_wire_contract.md`).

The initiator's core1 caches what it received so it can report an edge rather
than waking core0 at the poll rate. A second arrival path puts a value into
core0's appliers without that cache, and the cache then holds the previous
value permanently. **When an edge filter stands between a carrier and its
consumer, the carrier must have exactly one path to that filter.**

The responder direction is out of scope: it holds no cache of received
values. Suppression belongs on the sending side because the sent-state
shadow retires from the wire's own section byte.

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

Protocol and which relations admit the lane:
`era_host_peer_storage_contract.md`.

Selection and enqueue run only from the cold core0 task
(`era_host_peer_storage_runtime_task()` in `split/era_host_peer_storage.c`,
called from `era_split_transport_scheduler_housekeeping_body()` in
`split/era_split_transport_scheduler.c`). Scan hooks consume only the cached
storage-active suppression token. The responder never selects a storage
route.

Initiator context (`era_host_peer_storage_context_peer()` in
`split/era_host_peer_storage.c`) holds only when all of: not CLEAN
quarantined; confirmed HOST-PEER PEER or DUAL-HOST Left with `peer_host_open`
and local EEPROM policy requested; owner ready; this half is the wire
initiator; peer known; both endpoints advertise bulk-page support. Submit
also refuses while `general_initiator_pending`, `status_revalidation_due`, or
a storage request is already pending.

Unpinned `BUSY`/core0 pin handoff is nonexclusive. Validated `TRANSFER`
clamps owner routes and the grant enable bit as a mask down to
`ATTACH_STATUS`. **Transfer exclusivity lifts at transfer-verified.** A push
initiator then keeps only the standing grant disabled until COMPLETE while the
remote responder may be inside synchronous Apply. Cancel on authority,
relation, policy, owner, or source-revision change.

Arbitration exchanges are nonexclusive at the storage retry cadence.
**Initiation is change-triggered, never periodic.** Three triggers: relation
(re)establishment, this half's settled capture, a responder-changed signal.
News carrier = HSRSP storage news section in both relations
(`era_host_peer_storage_contract.md` **Storage News Value And Relation-Open Audit**).
**The hint is consumed as news, never as a level.**

Post-push identity rotation reopens via `SESSION_STATUS`. Close/abort after
an exclusive transfer flushes the HOST-PEER matrix relation; `MATCH` does
not. **There is one storage lane, not one per relation.**

## SESSION_STATUS Discovery And Liveness

`SESSION_STATUS` is the discovery/revalidation route and nothing else. The
advisory bit it used to carry is retired (`era_identifier_map.md`).

`era_split_transport_scheduler_relation_lane_live()` in
`split/scheduler/era_split_transport_scheduler_timing.c` is true for a
HOST-PEER PEER with local `matrix_ready`, or a DUAL-HOST Left, when AUTHORITY
is eligible in both directions. That predicate suppresses the periodic
`SESSION_STATUS` beat and in-relation edge revalidation together.

| Rule | Value |
| --- | --- |
| no sync policy gate | discovery, role change, recovery always reachable |
| known-relation idle liveness | the relation's own lane, never `SESSION_STATUS` |
| peer-unknown bootstrap | `ERA_SPLIT_WIRE_BOOTSTRAP_PERIOD_MS` 25 → `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS` 500 after `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER` 10 consecutive misses |
| accepted traffic refreshes liveness regardless of class | `era_invariants.md` |

**Every half opens the wire at Low.** Boot Low is
`era_split_transport_scheduler_start_communication_core()` in
`split/era_split_transport_scheduler.c`. A peer-unknown responder with no
serviced relation still steps toward a rate it hears; the peer-unknown
initiator never moves off *running*.

The listener ring, its constants, and every convergence case live in
`split/era_split_link.h` **Reconciliation**. **The cable carries power from
the hosted half**, so every plug is a late-peer case: threshold
`ERA_SPLIT_LINK_SCAN_NOISE_MIN` 2. Ring High → Medium → Low → High. Dwell
`ERA_SPLIT_LINK_SCAN_DWELL_MS` 1500. The three route-layer terms: settled
wire role ANDed with wire availability; whether the plan is the peer-unknown
bootstrap; whether a relation is serviced. The step runs in
`era_split_transport_scheduler_apply_link_step()` in
`split/era_split_transport_scheduler.c` (backend teardown-and-rebuild). A
silence floor recorded a working level as failed; the refusal lives in
`split/era_split_link.h` **Reconciliation**.

**A serviced relation raises to the winner's stored level at a shared-clock
deadline.** The relation opened means `SESSION_STATUS` confirmed both halves
at Low. The standing exchange carries the time-anchor that puts T_commit in
one domain. The raise is `era_split_transport_scheduler_apply_link_level()`
in `split/era_split_transport_scheduler.c` with relation identity kept;
`era_split_transaction_backend_wire_scale()` in
`split/era_split_transaction_backend_rp2040.c` follows that `set_speed`. A
failed raise reverts *running* to Low and stores nothing. First EEPROM SYNC
waits for `era_split_link_runtime_settled()` (`split/era_host_peer_storage.c`).
Winner = DUAL-HOST Left / HOST-PEER HOST. **Left remains the one
arbitrary-tie answer** (`era_host_peer_storage_contract.md`'s **Arbitration**,
`split/era_split_restart_agreement.h`).

## HOST-PEER PEER Priority

Core0 selects two things, in this order
(`era_split_wire_router_select_owner()` in `split/era_split_wire_router.c`):

1. Pending `SESSION_STATUS` bootstrap, policy-generation, or recovery
   revalidation.
2. `HOST_PEER_SOURCE_PUSH` when PEER matrix dirty or forced baseline exists.

Active storage is not a router arm: the due-flag mask and the
generic-vs-storage publication gate in **Common Route Priority** keep it
ahead of matrix without selecting it here.

**The standing exchange is not a fourth entry.** Core1 reaches it only when
the request queue and the storage lane both did nothing; the matrix
source-push preempts it there.

Healthy HOST-PEER traffic MUST suppress the duplicate periodic
`SESSION_STATUS` refresh, but MUST NOT suppress discovery, liveness, or
pending revalidation.

## HOST-PEER HOST

| Rule |
| --- |
| Does not select independent routes; responds only to admitted PEER requests |
| Accepted heartbeat/source-push refreshes liveness; source-push updates the HOST-owned peer matrix cache |
| No HOST-owned section open or due → one-byte `HOST_PEER_ACK_STATUS` (`era_invariants.md`) |
| `HOST_PEER_HOST_SOURCE_RSP` response-slot only (`era_closed_surface_contract.md`); eligibility in `era_wire_contract.md` |
| HOST responder sees an ordinary heartbeat and cannot tell which core timed it |
| A busy HOST answers that slot with the section-less ACK |

## DUAL-HOST

Ownership table: `era_authority_contract.md`. Matrix/digest/mirror stay
closed (`era_invariants.md`). Storage: Left initiator, Right responder.
Runtime open: Left grants both runtime reasons; Right grants nothing
(`era_split_transport_scheduler_build_standing_plan()` in
`split/era_split_transport_scheduler.c` returns a memset plan unless this
half is the wire initiator).

Every runtime section rides the standing exchange and adds no cadence.
Poll is unconditional. Left marks `ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH`
for INPUT edge, activity change, and visual baseline
(`era_split_transport_scheduler_note_local_layer_change()`,
`era_split_transport_scheduler_note_local_activity_change()`,
`era_split_transport_scheduler_note_local_visual_change()` in
`split/era_split_transport_scheduler.c`). Right's changes mark the responder
snapshot due.

## Stale Recovery

- Watch counts every accepted frame, no exemptions (`era_invariants.md`).
- **`ERA_SPLIT_RESPONDER_SILENCE_MS` 100 is canonical here.** Both poll
  periods asserted to half. `ERA_SPLIT_SESSION_STALE_MS` 0 selects it
  (`era_split_transport_scheduler_relation_stale_ms()` in
  `scheduler/era_split_transport_scheduler_timing.c`). Every "100 ms
  responder-silence limit" elsewhere is an application of this rule.
- Local HOST close invalidates both-hosted facts → peer-unknown bootstrap.
- Failure sites mark dirty/due only. Cable removal → relation unknown.

## Due/Deadline Model

The transport scheduler is event-driven plus deadline/due. Positive events
mark dirty/due; absence and liveness are deadline decisions.

| Sensor | Word | Site | File |
| --- | --- | --- | --- |
| responder-silence watch | accepted RX | receiving half | **Stale Recovery** |
| initiator silence watch | completed Core1 lifecycle-loop count via `era_split_communication_core_progress_count()` | initiating half | `scheduler/era_split_transport_scheduler_timing.c`, `split/communication_core/era_split_communication_core_lifecycle_rp2040.c` |
| in-flight expiry | pending initiator request neither consumed nor expired | housekeeping body | `split/era_split_transport_scheduler.c` |

Initiator-watch arms only when: initiator role, live `CORE1` lease, known
peer, no stop report, and a **granted** standing plan
(`standing_plan_granted`: `relation_generation != 0`, cached at publish).
Ungated, the watch fires every bound on a healthy powered-without-USB pair.

Bound: `ERA_SPLIT_CORE1_UNRESPONSIVE_MS` 100 ==
`ERA_SPLIT_RESPONDER_SILENCE_MS` 100, never the
`ERA_SPLIT_SESSION_STALE_MS` 0 override. `_Static_assert` holds 2 ×
`ERA_SPLIT_STANDING_LIVENESS_MS` 50 inside it, at equality, and
`ERA_SPLIT_STANDING_LIVENESS_MS` 50 + 2 ×
`ERA_SPLIT_COMMUNICATION_CORE_STORAGE_RESPONSE_WINDOW_MS` 25 at equality.
Two-stage convergence: silence fire replans the mode; judgment
(declare-dead, relaunch) arrives through the expiry. Judgment is
owner-layer.

Responder RX may block; role/reset/admission/quiesce MUST have an explicit
wake or cancel. On RP2040 core1, idle wait authority is the PIO RX FIFO plus
error/deadline/cancel/owner-epoch predicates — no ChibiOS thread wait or
resume. PEER matrix dirty may mark `HOST_PEER_SOURCE_PUSH` due without full
planning. **No runtime route deadline sits in the core0 deadline set.**

**The raw pre-filter is never the authority.** The deadline arm compares the
raw hardware microsecond counter against a stamp published beside the
millisecond deadline (`scheduler/era_split_transport_scheduler_timing.c`).
The stamp lands early, never late, so an open pre-filter still owes
`timer_expired32()` against `next_scheduler_deadline_ms`.
`timer_read32()` (`platforms/chibios/timer.c`) is the millisecond authority;
the pre-filter reads `timer_hw->timerawl`. **There is one clock.**
`CH_CFG_ST_FREQUENCY` is 1000000 on the RP2040 board configs this image uses
(`platforms/chibios/boards/QMK_BLOK/configs/chconf.h`,
`platforms/chibios/boards/QMK_PM2040/configs/chconf.h`). That counter is
`TIMER->TIMERAWL`.

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

### Latch Concurrency Rules

Producers run in housekeeping, matrix hooks, VIA, suspend/wakeup, and USB
device-state notification.

- Producers set latches OR-only, under `ATOMIC_BLOCK_RESTORESTATE`
  (`era_split_transport_scheduler_mark_dirty()`,
  `era_split_transport_scheduler_mark_route_due()`,
  `era_split_transport_scheduler_mark_maintenance_due()` in
  `split/era_split_transport_scheduler.c`). Never clear; never call scheduler
  planning.
- Consumer: atomic read-and-clear
  (`era_split_transport_scheduler_consume_pending_dirty_flags()`,
  `era_split_transport_scheduler_consume_maintenance_due_flags()` in that
  file).
- Consume as an edge, not a level. **When a level's clear is derived from
  something the consumer also observes, prefer a value the producer can only
  move forward.** Hint history:
  `era_host_peer_storage_contract.md` **Storage News Value And Relation-Open Audit**.
- Do not mark a latch from raw USB/SOF before the reducer commits, or from a
  policy setter before the new generation commits.
- Init seeds a full snapshot.
