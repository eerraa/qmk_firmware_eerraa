# ERA Authority Contract

Genre: contract
Canonical for: relation authority and its derivation, local session facts, the
persisted sync-policy bits and their EEPROM block, matrix readiness, storage
direction authority, revalidation policy, and the authority/scheduler boundary

## Local Authority Facts

| Fact | Rule |
| --- | --- |
| `accepted_host_open` / `accepted_no_host` | a valid local session holds exactly one |
| `bulk_page_supported` | compile-time capability on the same session status, not a session fact (Replacement Storage Authority) |

| Close → `accepted_no_host` | Predicate |
| --- | --- |
| sustained suspend | past `ERA_SPLIT_AUTHORITY_SUSPEND_HOST_GRACE_MS` 500. Short suspend is the same HOST in low power, not a close. On RP2040 ChibiOS USB, indefinite suspend without resume/configured evidence can look like split-cable-powered removal |
| suspend, no configuration | not a valid HOST suspend |
| residual | not CONFIGURED, or SOF older than `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10 (`split/era_split_authority_reducer.c`) |

| Rule | Site |
| --- | --- |
| Single derivation: the reducer, initialized at `keyboard_pre_init_kb`. No boot latch, no cold-boot master probe | `split/era_split_authority_reducer.c` |
| `is_keyboard_master()` and `is_keyboard_master_impl()` are one-line projections of `accepted_local_host_open`; the `split_config.master` the second fills is read by nothing | `split/era_split_authority_reducer.c` |
| Handedness latches from `SPLIT_HAND_PIN`, not `is_keyboard_left()`. QMK does not fill `split_config.left` until `split_pre_init()`, after the reducer exists; reading the QMK projection at init would leave both halves responder | `quantum/split_common/split_util.c` |
| Firmware USB re-enumeration is not a HOST close and not lighting sleep. VIA Apply bounces the bus (`split/era_split_via_link.c`); `era_usb_session_note_firmware_reattach()` holds host-open and the frame-loss arm for that window | `system/era_usb_session.c` |
| Suspend grace and host-open close govern authority only. Local lighting sleep MUST NOT be gated on host-open and must persist through the authority close a sustained suspend causes | **Lighting Sleep Ownership** |

> **REFUSED:** removing the `is_keyboard_master_impl()` override, or selecting RP2040 split role from raw VBUS (`USB_VBUS_PIN`, or a build without `SPLIT_USB_DETECT`).
> **WHY:** QMK's weak `is_keyboard_master_impl()` in `quantum/split_common/split_util.c` calls `usb_disconnect()` on a judged non-master, and without `SPLIT_USB_DETECT` it reads `usb_vbus_state()`; either change drops D+ at boot or makes VBUS the live role source the moment the override is gone.
> **REOPENS:** that weak override no longer disconnects or reads VBUS, and ERA derives role from something other than the reducer.

Absence of a `usb_disconnect` symbol is the Source Gate check
(`era_performance_gates.md`). Neither selector is read by ERA now; both stay so the fallback stays dead.

| Remote wake | Rule |
| --- | --- |
| Inputs | exactly two local facts: session (bus suspended, host granted remote wake) and the composed-matrix input edge (`split/era_split_keyboard.c`) |
| Ungated | MUST NOT be gated on host-open, `is_keyboard_master()` (`split/era_split_authority_reducer.c`), or the action pipeline |
| Consume | waking input is consumed locally, not forwarded as ordinary key input |
| PEER key | reaches the trigger only as the HOST's composed-matrix edge; transport receive must not inject HID or push raw USB wake state. That input is produced by the relation — **Relation Hold**. Device-reported 2026-08-13: HOST key wakes, PEER key does not |

> **REFUSED:** a USB remote-wake requester behind the action pipeline.
> **WHY:** the grace close makes `is_keyboard_master()` false, so QMK's `should_process_keypress()` (`quantum/keyboard.c`) holds `process_record()` (`quantum/action.c`) closed for the rest of a sustained suspend, and the requester is then unreachable exactly when the wake is needed.
> **REOPENS:** QMK no longer gates `process_record()` on host-open during suspend, or remote wake no longer consumes a composed-matrix edge.

Sleep predicate = two detectors of one physical event (explicit suspend and
frame loss) in `system/era_usb_session.c`. Model:
`era_board_adoption.md` **Non-Split Board Baseline**.

## Lighting Sleep Ownership

**A half's lighting sleep has exactly one owner, and which one is a property of
the relation rather than of the USB stack:**

| Relation role | Owner |
| --- | --- |
| `LOCAL_NO_LINK`, both DUAL-HOST roles, the HOST-PEER **HOST** | its own USB session |
| the HOST-PEER **PEER** | the wire — the relation's HOST |

**The non-owner does not write the render gate.** A HOST-PEER pair shares the
HOST's USB session, so on the PEER the local predicate has no subject
(`split/era_split_keyboard.c`).

| Edge | Rule |
| --- | --- |
| local → wire (demoted into a PEER) | hold no locally-decided value; resolve **lit**. Dark-until-told is the defect; rotation drops the responder RGB sent shadow, so the HOST answer is due on the first response |
| wire → local (promotion, or becoming DUAL-HOST) | drop the wire's last word; this half's session decides from the next pass |

Device-reported 2026-08-13: a demoted DUAL-HOST→PEER half stayed dark until
a key on the other half — grounds resolve-lit.

A board status report may punch the render gate for 3.68 s (core1
launch-failure, `split/communication_core/era_split_communication_core_launch_signal.c`).
Override, not a second owner: it re-asserts while it runs; the resolver
writes only on an edge of its own value. **A HOST-PEER HOST publishes the
resolved decision and never the raw gate.**

## Persisted Sync Policy

No DUAL-HOST parent switch and no per-relation EEPROM request. One
relation-independent requested bit per family. Persisting a requested value
opens no route, payload, or execution.

| Family | Scope | Arms | Default |
| --- | --- | --- | --- |
| EEPROM | the one request replacement storage consumes in every admitted relation; DUAL-HOST support is `requested` alone | one bit, one engine | ON |
| RGB | DUAL-HOST RGB runtime: config sections and visual pressed-baseline family (reactive half), both directions. Effect = `requested &&` eligible (`era_wire_contract.md`). HOST-PEER RGB/visual ungated — dark PEER mirroring HOST render is that relation's correctness path | sender bit gates capture/arming; receiver bit gates apply (standing visual apply still advances its sequence shadow) | ON since v5 (owner decision 2026-08-13). Off = explicit acceptance of independently rendering halves |
| INPUT | exactly DUAL-HOST INPUT-class: layer byte and ACTIVITY, both directions. HOST-PEER has no INPUT-class section (asserted) | RGB pattern, plus: off **sender substitutes the neutral value** (layer 0, zero activity); disable edge clears peer-derived INPUT; enable edge re-observes standing state | ON from v4 — absence is the cross-half layer defect the class exists to fix |

> **REFUSED:** syncing the split link level through this policy block.
> **WHY:** a sync policy is an owner preference each half may hold differently, but the wire must run one level or no session opens, and putting the stored byte here would gate that pair invariant on a switchable preference and give one byte two writers.
> **REOPENS:** the running level becomes an owner preference that halves may hold differently without opening a session.

The level is persisted per half and sync-excluded;
`split/era_split_link.h` **Reconciliation** meets at Low and raises to the
winner's stored level. Units: `era_source_map.md`. Wire: `era_wire_contract.md`.

Nothing in the protected range travels, so an EEPROM clean is an agreed
restart. A one-half clean converges the seven domains
(`era_host_peer_storage_contract.md` **Arbitration**) and leaves `176..255`
split. Both-halves erase:
`era_host_peer_storage_contract.md` **Why An EEPROM Clean Is An Agreed Restart**.

Local-policy block at `ERA_EEPROM_CONFIG_ADDR +
ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET` (`storage/era_eeprom_layout.h`;
offsets in `split/era_split_sync_policy.c`):

| Offset | Content |
| --- | --- |
| `176`, size `24` | block; starts the protected range |
| `+0` | signature. Change resets the block at boot; supersedes the retired dual-host-only signature |
| `+4` | flags generation (any requested change) |
| `+6` | EEPROM policy generation (EEPROM bit only — INPUT/RGB cannot invalidate an in-flight storage generation match) |
| `+8` | version: **5, the only accepted value** (`ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION` in `split/era_split_sync_storage.h`) |
| `+9` | requested flags EEPROM, INPUT, RGB — **all three default on** |
| `+10..+23` | seven 16-bit LE divergence counters (`ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET` 10, `ERA_SPLIT_SYNC_POLICY_STORAGE_PREFIX_BYTES` 10). `era_host_peer_storage_contract.md` **Recency Layer**. Ordinary persist writes `+0..+9` only; counters carry no validity meaning here |

**There is no in-place upgrade. Version 5 is the only accepted value**
(owner decision). An invalid signature, flag mask, generations, or version
fails the block and rewrites it **whole** — defaults, fresh signature,
version 5, counters zeroed. Recency degrades conservatively
(`era_host_peer_storage_contract.md` **Arbitration**).

> **REFUSED:** an in-place upgrade of an earlier sync-policy block, or any accepted version other than 5.
> **WHY:** a valid block is loaded verbatim and an ordinary persist writes only the `+0..+9` prefix, so a partial rewrite would leave foreign counter bytes under a valid signature.
> **REOPENS:** a converter that rewrites the whole 24-byte block under a new version, including counters, with a traced class-A fallback.

**A changed default rides the version byte.** A valid block is loaded
verbatim, so a default nothing rewrites reaches only never-booted hardware.
Version 4 carried INPUT's default-on; version 5 carries RGB's.

## Replacement Storage Authority

Domain and transaction: `era_host_peer_storage_contract.md`. Governs every
relation the lane is admitted for. `host_peer` in names is historical
(`era_source_map.md`).

| Rule | Contract |
| --- | --- |
| Direction | per-domain latest-change-wins: one-sided wins that side; both-changed by persisted divergence count then Left; winner's whole-domain content under existing CRC/generation transfer (`era_host_peer_storage_contract.md`) |
| Initiation | the relation's initiator only — PEER in HOST-PEER, Left in DUAL-HOST — for pull, push, and arbitration. Responder data/ack only in the admitted slot; responder independent send closed. Arbitration picks content, never the initiator |
| Admission | `era_host_peer_storage_contract.md` **Relation Admission** |
| VIA toggle | each half keeps its EEPROM requested bit and generation. Initiator off → no select. Responder off → no data, may report `POLICY_CLOSED` in an admitted proof |
| Identity | one engine, one identity space. A relation change rotates the lane's generation/authority/state/diagnostics |
| Match set | owner epoch, relation/request/EEPROM-policy/storage-transaction generation, domain/schema, source revision, image integrity; chunks also match chunk identity |
| Completion | wire or final-chunk receipt is transfer only. Applying half's core0 owns durable completion (initiator pull, responder push) after guarded mutation, read-back, reload, and that lane's identity rotation |
| Supersession | responder source supersession is a data-generation event, not a relation-authority change; no responder initiation, no forced `SESSION_STATUS` |
| `bulk_page_supported` | `true` iff the build compiles decoder, dedicated request/result capacity, domain table, and guarded core0 apply, mirroring `ERA_HOST_PEER_STORAGE_V1_ENABLE`. Not board-scoped. Grants no route and no responder independent-send. Wire: `era_wire_contract.md` |

> **REFUSED:** a fixed HOST-source / PEER-target winner for replacement storage.
> **WHY:** in DUAL-HOST both halves enumerate and either can take a VIA edit, so a fixed winner discards whichever half the user did not edit second; HOST-PEER steady state is unaffected — a PEER takes no VIA edits — but divergent-history reopens would revert newer content.
> **REOPENS:** DUAL-HOST storage admits edits from only one half.

## Matrix Ready

| `SESSION_STATUS.matrix_ready` | When |
| --- | --- |
| MUST be true only when | local session valid; `accepted_no_host=true`; HOST-PEER fast-path local matrix publisher has a publishable snapshot ready |
| MUST be false for | HOST-open; DUAL-HOST; invalid or unknown local authority |

## HOST-PEER Matrix Admission

HOST accepts HOST-PEER matrix payloads only when all hold
(`split/era_split_scheduler_session.c`): local relation is HOST-PEER HOST;
local session valid; peer session known; peer `accepted_host_open=false`;
peer `accepted_no_host=true`; peer `matrix_ready=true`.

**The first term is the whole of the role term.** A suspended HOST is still
its relation's HOST and still owns the projection its remote wake reads;
closed host-open governs HID emit, which QMK's suspended
`should_process_keypress()` (`quantum/keyboard.c`) already holds.

> **REFUSED:** a second copy of the HOST-PEER role term derived from `accepted_host_open`.
> **WHY:** **Relation Hold** lets the relation outlive host-open, so asking host-open here folds "am I this relation's HOST" into "may I type right now", and only the second is what a sustained suspend answers.
> **REOPENS:** the relation can no longer outlive local host-open.

## HOST-PEER HOST Source Response Admission

Response-slot only, once a concrete section opens: local relation is
HOST-PEER HOST; the PEER request already passed HOST-PEER
heartbeat/source-push admission; response ACK sequence matches the admitted
PEER request. No opened HOST-source section → one-byte `HOST_PEER_ACK_STATUS`;
HOST independent send forbidden. Sections vs bare ACK:
`era_route_contract.md` **One carrier for the response section set**.
Eligibility: `era_wire_contract.md`.

## Relation Hold

**Both halves reporting no host is the absence of the fact that assigns the
roles, not a fact that unassigns them.** Naming rules derive the pair from a
half that owns a USB session; with none, the current relation is held. A PEER
with no host of its own does not become HOST because the HOST's host went
away. Ends on enumerate or peer stale (`split/era_split_mode_planner.c`).

| Authority pair | Liveness | Next relation |
| --- | --- | --- |
| local-open + peer-open | live | DUAL-HOST (Left / Right by side) |
| local-open + peer-no-host | live | HOST-PEER HOST |
| local-no-host + peer-open | live | HOST-PEER PEER |
| local-no-host + peer-no-host | live | **hold** current relation |
| any | stale / unknown | reset: `LOCAL_NO_LINK` / peer-unknown |

Assignment, hold, and staleness are one transition function: naming arms
re-role the pair; the hold arm returns it; staleness resets the fold. The
relation is a fold over authority *edges*. Downstream facts — **Initiator
Authority** first — project that one result.

A held relation keeps its standing exchange —
`ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS` 10 (HOST-PEER),
`ERA_SPLIT_DUAL_RUNTIME_POLL_MS` 1 (DUAL-HOST) — indefinitely if the host
never resumes (periods: `era_route_contract.md`). PEER-key wake fires on
state only that exchange produces. Cost is core1 and the wire, beside a half
that keeps scanning at the ≥18 kHz floor (`era_performance_gates.md`).

A ≥`ERA_SPLIT_RESPONDER_SILENCE_MS` 100 wire outage, or a core1 death, inside
a held span costs the rest of that suspend its PEER-key wake (`LOCAL_NO_LINK`
hold). HOST-key wake survives on the local composed edge.

A sustained suspend is the ordinary way in: the grace close no longer takes
the relation with it (**Local Authority Facts**, remote wake). Forced
peer-session stale recovery (**Revalidation Authority**) and matrix
admission (**HOST-PEER Matrix Admission**) are the same rule at two other
sites; re-deriving any from `accepted_host_open` reopens this section.

## Initiator Authority

**The initiator is a projection of the relation and not a second derivation of
it.** Decided with the relation, from the relation, in one plan
(`split/era_split_mode_planner.c`). A held Left HOST would otherwise take the
initiator role from under a PEER still pushing to it.

| Relation state | Initiator | Responder | Matrix route |
| --- | --- | --- | --- |
| Peer unknown | Left only | Right | `SESSION_STATUS` only |
| HOST-PEER | PEER | HOST | PEER source-push only |
| DUAL-HOST | Left | Right | forbidden |
| Right standalone/no peer | none | optional responder | none |

Matrix column = matrix route only. Runtime uses the same assignment:
HOST-PEER PEER-push / HOST-response; DUAL-HOST Left-push / Right-response;
peer-unknown and standalone run it not at all. Sections:
`era_wire_contract.md`.

## Revalidation Authority

| Rule | Contract |
| --- | --- |
| `SESSION_STATUS` | discovery, bootstrap, recovery only. Not a sync child. Role and policy/generation revalidation ride each relation's AUTHORITY section (`era_wire_contract.md`) |
| In-relation authority edge | raises no pending revalidation when that lane already carries the section. The frame has no cadence |
| Two edges keep the frame | a relation that **changed**, and **staleness**. Peer-unknown bootstrap is neither and runs the frame unchanged |
| Both directions | every serviced relation carries AUTHORITY; a responder cannot initiate, so the response direction has no alternative. Overlap costs one seven-byte section per authority edge, not a period. Body/eligibility/deferral: `era_wire_contract.md` |
| Poll period | derived. `ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS` is defined to `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10. `_Static_assert` in `scheduler/era_split_transport_scheduler_internal.h`. Stopped-counter detect 10–20 ms. Any value under the 2048 ms wrap would detect; the derivation stops the period drifting off the window (`split/era_split_authority_reducer.h`) |
| Liveness | heartbeat/source-push liveness carries no session facts; accepted relation traffic may refresh stale detection |
| `local_status_pending` | MUST be cleared by successful `SESSION_STATUS`, not HOST-PEER ACK alone |
| Failure | fact staleness or payload validation failure → peer-unknown `SESSION_STATUS` bootstrap/backoff |
| Forced stale | HOST-open → no-HOST **and the peer is hosted** → force peer-session stale recovery to peer-unknown LEFT-initiator/RIGHT-responder bootstrap (`split/era_split_transport_scheduler.c`) |

> **REFUSED:** forcing peer-session stale recovery against a peer that is unambiguously no-host.
> **WHY:** that peer was the relation's initiator before the close and still is, and it cannot become the HOST by an edge on this half, so forcing a recovery forgot the peer session and tore down a HOST-PEER relation whose roles were never in doubt, once per computer sleep (**Relation Hold**).
> **REOPENS:** an unambiguous no-host peer can claim the HOST role from a local close.

A peer-unknown or otherwise unusable peer session keeps the forced recovery.

## Authority Scheduler Boundary

| Rule | Contract |
| --- | --- |
| SOF movement | not scheduler dirty state by itself |
| SOF loss / recovery | deadline/due. **This is the architecture's one poll** (`era_overview.md`) |
| Authority result change | marks authority dirty for scheduler planning |
| Raw facts | raw authority, SOF, or USB host-state facts must not be pushed to the peer |
| Suspend | SOF loss during USB suspend must not collapse an accepted local HOST relation before the grace window; the three `accepted_no_host` closes are in **Local Authority Facts** |
| Suspend close | USB stack state plus the reducer's bounded suspend duration, not raw VBUS |
| Startup loop | QMK's USB suspended startup loop stays disabled so a no-local-USB PEER keeps running `keyboard_task()` (`quantum/keyboard.c`). `NO_USB_STARTUP_CHECK=yes` in `split/era_split_qmk_rules.mk` |

> **REFUSED:** event-driving the SOF authority poll.
> **WHY:** the SOF counter is a 1 kHz host heartbeat with no loss interrupt, so its absence is observable only by noticing the count stopped; an event-driven poll believes a dead host behind a live cable is still a host.
> **REOPENS:** the USB stack raises a loss interrupt that is the sole observation of a stopped SOF counter.
