# ERA Route Contract

Genre: contract
Canonical for: route priority and admission, service cadence requirements, the
standing exchange grant, storage exclusivity, restart/deadline ordering, stale
recovery, and scheduler hot-path boundaries

Wire markers, bodies, and section eligibility are owned by
`era_wire_contract.md`. Storage protocol and durable-apply rules are owned by
`era_host_peer_storage_contract.md`. Authority samples and `matrix_ready` are
owned by `era_authority_contract.md`. Link-level reconciliation cases and the
stored speed ring are owned by `split/era_split_link.h` **Reconciliation**.
Implementation entry points are in `maps/era_source_map.md`.

## Common Route Priority

At transaction boundaries the initiator orders work as follows:

1. required bootstrap, policy, or recovery revalidation;
2. an admitted exclusive storage transfer while its relation is valid;
3. the relation's normal route;
4. no-op.

An exchange already begun is never preempted. Storage exclusivity suppresses
normal initiator traffic but never mandatory relation recovery or a responder's
answer to an already admitted slot. During transfer exclusivity that non-storage
answer is section-less; exclusivity must not open runtime response sections.
Generic and storage requests must not be published at the same time, and
suppressed due facts remain armed for later service.

A Core1 request becomes fresh when it is published to shared execution, not
when Core0 starts constructing it. Queue residence is bounded to 25 / 45 /
85 ms at High / Medium / Low and must remain below the 100 ms Core1
unresponsive bound. The selected transaction receives its own response window
after BEGIN; queue time is not charged to that transaction window.

Storage exclusivity covers validated TRANSFER through transfer verification and
bounded abort cleanup, not the whole storage episode. A push initiator may keep
the standing grant suppressed through COMPLETE while the remote responder can
still be inside synchronous Apply. CLEAN quarantine is stronger: once CLEAN is
selected for agreement, storage select/publish/admit/answer/snapshot remain
closed through COMMIT and controlled reset, while SESSION_STATUS and the
standing exchange remain available. The reason CLEAN requires that restart is
owned by `era_host_peer_storage_contract.md` **Why An EEPROM Clean Is An Agreed Restart**.

Heartbeat admission and matrix admission are deliberately different. A busy
HOST must still answer the relation's section-less liveness slot; storage may
suppress matrix/source traffic but must not make a healthy relation refuse its
liveness exchange.

Source anchors: `split/era_host_peer_storage.c`,
`split/era_host_peer_storage_standing_policy.h`,
`split/scheduler/era_split_transport_scheduler_routes.c`, and
`split/scheduler/era_split_transport_scheduler_responder.c`.

## Route Kind And Reason

Route kind is the wire operation. Route reason is the initiator-local cause.
The responder only observes the admitted wire operation and its own dirty
state.

| Reason | Wire operation | Required trigger |
| --- | --- | --- |
| ATTACH_STATUS revalidation | SESSION_STATUS | bootstrap, policy, discovery, recovery |
| HOST-PEER matrix source push | DELTA/FULL/RESET, with FENCE/BIND at incarnation boundaries | PEER matrix edge or fresh baseline; no cadence |
| storage idle proof | storage probe | relation-open audit, settled local capture, or responder-changed news; no periodic patrol |
| storage active | storage chunk/apply/complete or abort cleanup | validated transfer episode |
| runtime section push | HOST_PEER_SOURCE_PUSH with a runtime mask | local eligible section changed from last confirmed |
| runtime response poll | HOST_PEER_HEARTBEAT | initiator opens the responder's standing response slot |

Storage remains a dedicated cold-task lane rather than a generic route reason.
Putting storage through the generic owner ring is rejected because it would
collapse the independent admission/exclusivity boundary; revisit that choice
only if storage traffic must share the generic initiator ring. The two runtime
reasons belong only to the standing exchange.

Required route timing:

| Requirement | Value |
| --- | ---: |
| known-peer SESSION_STATUS refresh while the relation lane is not live | 50 ms |
| peer-unknown discovery before backoff | 25 ms |
| peer-unknown discovery after 10 consecutive misses | 100 ms |
| peer-unknown SESSION_STATUS response window | 2 ms |
| known-peer response window | 20 ms |
| HOST-PEER standing period | 10 ms, independent of link level |
| DUAL-HOST standing period | 1 / 2 / 4 ms at High / Medium / Low |
| authority sample period and SOF freshness | 10 ms |
| standing liveness while cadence is disabled | 50 ms |
| responder/Core1 silence bound | 100 ms |
| relation time-anchor refresh | 60 s |
| restart arm timeout | 60 ms |
| restart commit lead | 120 ms |

There is no post-relation SESSION_STATUS cadence while the relation's own lane
is live, and no periodic core0 route in a live relation. Storage retry, dirty
capture, episode, and backoff values remain owned by
`era_host_peer_storage_contract.md`. Link scan dwell, noise threshold, upgrade
confirmation, and stored-level rules remain owned by
`split/era_split_link.h` **Reconciliation**.

A new service reason must establish its wire surface, owner, trigger/cadence,
admission, and failure behavior before execution is opened for it.

## Runtime Execution Owner

Core1 executes the standing exchange on its own deadline. Core0 does not poll
Core1; it wakes for publication edges such as a completed request, responder
result, or changed standing state. Route authority therefore does not imply
Core0 execution ownership.

Repeated responder HEARTBEATs may reuse the same immutable snapshot while
Core0 is unavailable, but at most one successful result for the same
owner/relation/snapshot/section-mask identity may occupy the general result
path. SESSION and push results are not coalesced.

Core1 still reads no live QMK/VIA/EEPROM/RGB/matrix/USB/HID state. Core0
publishes immutable section bodies; Core1 only serializes admitted values.
The lifetime/thread boundary is normative in `era_invariants.md` **Core Lifetime
And Thread Boundary**.

### The standing exchange grant

Exactly one route kind may originate from Core1 on its own period in each
serviced relation. The grant covers only RUNTIME_RESPONSE_POLL and
RUNTIME_SECTION_PUSH.

- DUAL-HOST: Left owns the grant. Its period is 1 / 2 / 4 ms as link level
  moves High / Medium / Low.
- HOST-PEER: PEER owns the grant at 10 ms. The grant is withheld until
  `matrix_ready` is valid in both directions; reaching that state first causes
  status revalidation, so cadence does not start before the fact round-trips.
- The period is the only relation-specific term. Section eligibility remains a
  wire contract decision.

A standing plan is published on semantic change, never merely because a pass
or timer occurred. A field is populated only when the relation's eligibility
can carry it. Restart COMMIT uses one absolute shared-clock instant; republishing
the plan must not move that deadline.

A plan is executable only under a current owner epoch, a nonzero relation
generation, and a nonzero period. Clearing its enable bit stops normal cadence
but does not stop the 50 ms liveness exchange. Storage suppression and pending
status revalidation may clear enable without destroying the relation identity.

Standing state is latest-state, not a queue. Core0 owns plan publication;
Core1 owns received state and its change sequence. Old owner/relation identities
are rejected, and a newly accepted identity must notify Core0 even when its
first sparse response carries no changed section. Revocation must not erase the
Core1-owned receive state. When owner/relation identity changes, sent shadows
and received-section caches retire before the new identity is used, while
cumulative exchange/section counters and the visual-delivery sequence survive;
otherwise an owner-only restart can alias an older applied visual receipt.

A failed standing exchange creates a nonzero stop generation and halts Core1
origination. Only a matching decoded successful SESSION_STATUS recovery for the
captured owner/relation may acknowledge that stop. Stale success, an earlier
request, or an unrelated plan change cannot resume it. A stop raises
revalidation; it does not by itself declare the peer stale. A stop during
durable Apply is a relation-recovery event, not a persistence rollback. Core1
never retries around this handshake on its own.

Source anchors:
`split/era_split_transport_scheduler.c`,
`split/communication_core/era_split_communication_core_standing.c`, and
`split/scheduler/era_split_transport_scheduler_timing.c`.

#### One carrier for the response section set

A response section rides the standing exchange's answer and no other route.
A Core0-originated input request is answered with a typed identity receipt
and carries no runtime response section. Rare FENCE/BIND uses separate status
responses. Runtime sections still have only the standing answer carrier. Eligibility still decides whether a relation may
carry a section; this rule decides which carrier may deliver it.

The initiator's Core1 receive cache is an edge filter between wire state and
Core0. While that filter exists, a section must have exactly one delivery path
to it. A second path that applies directly in Core0 can leave the filter holding
an older value and turn a later legitimate value into a false duplicate. The
filter also keeps an unchanged DUAL-HOST section from waking Core0 at the
1 ms standing-poll rate.

Therefore:

- do not add a second response-section arrival path unless the edge-filter
  contract is redesigned with it;
- do not discard a section after the sender has advanced its sent shadow,
  because that is permanent loss rather than deferral;
- sender-side suppression is preferred when a section is unchanged.

This restriction may be revisited only if the carrier itself advertises
on-change semantics consistently in every relation or a second path is a
deliberate requirement with one coherent edge-filter owner.

## Replacement Storage Route

Storage selection and enqueue run only from the cold Core0 storage task. Scan
hooks may consume only cached storage-active suppression state; they do not
select, serialize, or inspect live storage.

Storage initiation is admitted only when all of the following hold:

- CLEAN quarantine is not active;
- the confirmed relation is HOST-PEER with PEER as initiator, or DUAL-HOST
  with Left as initiator, and the peer is host-open;
- the local EEPROM sync policy requests storage;
- the owner/lease is ready, the peer is known, and this half is the wire
  initiator;
- link reconciliation has settled before relation audit or first sync begins;
- both endpoints advertise the required bulk-page capability;
- no generic request, required status revalidation, or storage request is
  already pending.

BUSY and the Core0 pin handoff are nonexclusive. Validated TRANSFER is
exclusive; exclusivity lifts at transfer verification. A push initiator then
keeps only the standing grant suppressed through COMPLETE if the remote
responder may still be applying. Authority, relation, policy, owner, or source
revision change cancels the episode.

Storage arbitration is change-triggered, never a periodic patrol. Its triggers
are relation establishment/re-establishment, a newly settled local capture,
and responder-changed news. News is consumed as an edge, never as a level.
After an exclusive HOST-PEER transfer closes or aborts, relation recovery runs
through SESSION_STATUS before normal traffic resumes; a MATCH that never entered
exclusive transfer does not force that recovery. There is one storage lane
shared by both admitted relations.

Protocol details and durable completion are owned by
`era_host_peer_storage_contract.md`. Source anchors:
`split/era_host_peer_storage.c` and
`split/era_host_peer_storage_standing_policy.h`.

## SESSION_STATUS Discovery And Liveness

SESSION_STATUS is only discovery and revalidation. It does not carry ordinary
live-relation application state or a spare advisory bit.

A serviced relation uses its own lane for liveness. When HOST-PEER PEER has
local `matrix_ready` and AUTHORITY is eligible both ways, or DUAL-HOST Left has
AUTHORITY eligible both ways, periodic SESSION_STATUS and in-relation edge
revalidation are suppressed together. Accepted traffic of any admitted class
refreshes relation liveness.

For a peer-unknown initiator, discovery runs every 25 ms for the first 10
consecutive misses, then every 100 ms. Its receive window is 2 ms; known-peer
transactions use the 20 ms response window. Discovery and recovery are never
gated by the user's sync policy.

Every half opens the physical link at Low. Link-level search and the rate ring
are owned by `split/era_split_link.h` **Reconciliation**; route policy only
requires that a serviced relation be confirmed before an agreed raise and that
no new independent send or extra application cadence is introduced to perform
that convergence.

## Agreed Restart And Link Convergence

The restart mechanism is prepare-then-act at one absolute shared-clock
deadline. The initiator may not interleave two acts, and an accepted request
means only that agreement work may begin; confirmation, quiet requirements,
and the deadline still govern execution.

The arm may wait at most 60 ms. The selected COMMIT instant has 120 ms of lead
and must remain later than the arm timeout plus two HOST-PEER standing periods.
An act does not preempt an exchange already in progress. Once the timed window
is active, new opportunistic bank erase, cold storage capture, and new storage
episodes yield; already admitted work drains before the arm. Due execution
occurs after wire-result/disarm draining and before cold NVM work.

LINK_SPEED and LINK_RECOVERED keep USB connected and skip the raw-HID quiet
gate. EEPROM CLEAN is the reset-bearing act and owns its stronger physical
boot-replay and erase guarantees in
`era_host_peer_storage_contract.md` **Why An EEPROM Clean Is An Agreed Restart**.

A new serviced pair first meets at Low. The agreed winner is DUAL-HOST Left or
HOST-PEER HOST; the winner's stored level is raised at the shared deadline.
A failed raise returns the running link to Low and does not persist a guessed
rate. Reconciliation is owed once per physical meeting of the pair, not once
per relation-generation churn. Presentation-only LINK_RECOVERED follows the
same agreement boundary and adds no wire class, route cadence, or reset.

The act table and validators live in `split/era_split_restart_agreement.h` and
`split/era_split_keyboard.c`. Link convergence cases live in
`split/era_split_link.h` **Reconciliation**.

## Relation-Specific Ownership

### HOST-PEER PEER Priority

Core0 selects, in order:

1. pending SESSION_STATUS bootstrap/policy/recovery revalidation;
2. typed input for a dirty PEER matrix or fresh baseline.

The dedicated storage gate sits ahead of normal matrix work without becoming a
generic route arm. A due standing slot precedes queued ordinary input at the
next transaction boundary; FENCE/BIND remains contiguous. Other standing work
runs after higher-priority Core0 and storage transactions.

Healthy HOST-PEER traffic suppresses duplicate periodic SESSION_STATUS, but
must never suppress discovery, liveness, or pending revalidation.

### HOST-PEER HOST

The HOST selects no independent route. It responds only to an admitted PEER
slot. Accepted source-push updates the HOST-owned peer-matrix projection;
heartbeat/source-push traffic refreshes liveness. If no HOST-owned response
section is due, or storage prevents an ordinary section, the HOST still returns
the section-less ACK for the admitted liveness slot.

### DUAL-HOST

Left is the initiator and standing-grant owner; Right is responder. Matrix,
digest, and mirror execution remain closed by `era_invariants.md` and
`era_closed_surface_contract.md`. Runtime sections ride the standing exchange
only and add no second cadence.

A visual wake must derive from the physical/debounced local-row change, not
`process_record()` semantics. Tapping buffering and semantic filters do not own
the physical baseline. The scan path observes only a cached edge; packing the
baseline belongs on the due path. Source anchor:
`split/era_split_transport_scheduler.c`.

## Stale Recovery

The responder-silence bound is 100 ms. Standing periods must remain comfortably
inside that bound; a configurable session-stale override must not widen the
Core1 responsiveness bound.

Local HOST close invalidates both-hosted facts and returns discovery to
peer-unknown bootstrap. A transport failure marks dirty/due state; cable loss
makes the relation unknown rather than inventing a surviving peer fact.

The initiator-side Core1 silence watch is armed only while this half is the
wire initiator, holds a live Core1 initiator lease, knows the peer, has no
standing stop report, and has a granted standing plan. Legitimate quiet or
quiesced states disarm the watch instead of aging it.

## Due/Deadline Model

The scheduler is event-driven plus due/deadline state. Positive events set
dirty/due; absence and liveness are decided by deadlines. Runtime standing
cadence is owned by Core1 and therefore creates no runtime-route deadline in
the Core0 deadline set.

A blocking Core1 receive path must have an explicit wake/cancel path for role
change, reset, admission change, quiesce, or owner-epoch change. Silence
judgment is owner-layer policy, not a backend excuse to remain blocked.

The raw microsecond pre-filter may only avoid an unnecessary full deadline
check. It must never be the authority that declares a millisecond deadline
expired. The millisecond scheduler clock remains authoritative, because a
coarse pre-filter can otherwise fire work early and compound the error each
time a cadence re-arms.

Source anchors:
`split/scheduler/era_split_transport_scheduler_timing.c` and
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`.
The raw clock is owned by `platforms/chibios/timer.c` (`TIMERAWL`) and the
`QMK_BLOK` / `QMK_PM2040` board `chconf.h` files (`CH_CFG_ST_FREQUENCY`);
this contract does not duplicate their configured value.

### Hot-Path And Latch Boundary

Scan-path work is bounded and O(1). It may observe cached one-word edge/due
state and mark latches, but it must not allocate, scan broad source state,
recompute CRC/storage/RGB/INPUT freshness, serialize a standing plan, perform
cold storage selection, or build diagnostic snapshots. Those operations belong
to cold/due paths.

Producers from housekeeping, matrix hooks, VIA, suspend/wakeup, or USB state
changes OR dirty/due/maintenance latches atomically. Producers never clear a
latch and never run scheduler planning. The consumer performs atomic
read-and-clear.

Consume change notification as an edge rather than a sticky level. If a
consumer also observes the condition whose disappearance clears a level, prefer
a monotonic producer value so a missed clear cannot hide a later edge. Do not
arm route state from raw USB/SOF before the authority reducer commits, or from a
policy setter before the new generation commits. Initialization seeds a full
snapshot so the first real edge is not ambiguous.

Source anchors:
`split/era_split_transport_scheduler.c` and
`split/scheduler/era_split_transport_scheduler_timing.c`.

## Input pressure and fairness

`split/era_split_input.c` owns input history lifetime independently of the
relation and storage audit. A normal input-only ACK miss retains its immutable
request while born age remains; it does not mark the peer stale. Receiver WAIT
and ACCEPTED_STOP are capacity states. First HID overflow or history expiry
ends that input epoch and seeks fresh latest after capacity returns.

Pressure FLOW replaces the next HP standing op rather than adding a route or
confirmation poll. It retains the 10 ms cadence and runtime sections; quiet
length changes from one to two bytes in each direction. Normal idle and DH's
1/2/4 ms standing cadence remain unchanged. FLOW replies carry the source
producer generation captured before the exchange, so late hints cannot apply
to a new lifetime. FLOW_END idempotently closes live input; BASELINE_READY
resumes an unaccepted pending RESET without changing its identity, or permits
a new epoch after terminal recovery.
BASELINE_READY is a capacity hint, not an input-incarnation grant. An enabled
but closed receiver with room lets a waiting source attempt RESET, rejects it
with typed MISMATCH, and requires FENCE/BIND before accepting the baseline.
This rare receiver-only lifetime recovery costs one rejected RESET exchange;
ordinary capacity recovery does not reinstall A/B. A disabled gate stays shut.

Before an input TX, Core1 services one due standing slot at the frame boundary,
then constructs input control sequence and rechecks born deadline, owner,
relation, source generation and actual standing stop. Input queued before stop
cannot transmit around the existing exact SESSION stop handshake. FENCE/BIND
keeps standing origination closed until B completes. SESSION/storage priority,
silence windows, baud and storage admission are unchanged.
