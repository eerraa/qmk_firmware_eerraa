# ERA Authority Contract

Genre: contract
Canonical for: relation authority and its derivation, local session facts, the
persisted sync-policy bits and their EEPROM block, matrix readiness, storage
direction authority, revalidation policy, and the authority/scheduler boundary

## Local Authority Facts

One derivation: `era_split_authority_reducer_task()` in
`split/era_split_authority_reducer.c`. `keyboard_pre_init_kb()` in
`split/era_split_board.c` calls `era_split_keyboard_pre_init()` in
`split/era_split_keyboard.c`, which calls `era_split_authority_reducer_init()`,
before `split_pre_init()`. No boot latch. No cold-boot master probe.

`era_split_authority_reducer_get_snapshot()` publishes `valid`, `is_left`,
`usb_state` (`HOST_OPEN` / `NO_HOST` / `UNKNOWN`), and `usb_epoch` (starts at 1,
never 0; steps on every host-open edge). `era_split_scheduler_session_note_local_facts()`
in `split/era_split_scheduler_session.c` projects that onto session bits. A
valid session holds exactly one of `accepted_host_open` / `accepted_no_host`.
`bulk_page_supported` is compile-time (`ERA_HOST_PEER_STORAGE_V1_ENABLE`) on the
same status record, not a session fact; AUTHORITY does not carry it.

| Close → `accepted_no_host` | Predicate in `era_split_authority_host_open_for_state_locked()` |
| --- | --- |
| sustained suspend | configured `USB_DEVICE_STATE_SUSPEND` past `ERA_SPLIT_AUTHORITY_SUSPEND_HOST_GRACE_MS` 500. Short suspend keeps the same HOST. RP2040 ChibiOS USB can leave split-cable-powered removal looking like configured suspend |
| unconfigured suspend | `era_usb_session_configure_state()` in `system/era_usb_session.c` remaps it to `USB_DEVICE_STATE_INIT`; not a valid HOST suspend |
| residual | not `USB_DEVICE_STATE_CONFIGURED`, or SOF age ≥ `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10. Unavailable SOF is fresh here (`era_usb_session_sample_frame_age()` false → treat as fresh). Sample adjacent to this evaluation, never a prior pass's age |

Firmware USB re-enumeration is not a HOST close and not lighting sleep. VIA
Apply in `split/era_split_via_link.c` calls
`era_usb_session_note_firmware_reattach()` then bounces the bus; the reducer
and the frame-loss arm both ask `era_usb_session_firmware_reattach_hold()`
(`ERA_USB_SESSION_REATTACH_HOLD_MS` 2000). A rising host-open edge calls
`send_keyboard_report()`.

`is_keyboard_master()` and `is_keyboard_master_impl()` in
`split/era_split_authority_reducer.c` are one-line projections of
`accepted_local_host_open`. The `split_config.master` the second fills is read
by nothing: ERA overrides the weak `is_keyboard_master()` that would have read
it.

Handedness latches from `split_hand_pin_is_left()` (`quantum/split_common/split_util.c`),
not `is_keyboard_left()`. QMK does not fill `split_config.left` until
`split_pre_init()`, after the reducer exists; reading the QMK projection at
init would leave both halves responder.

MCU_RP builds require `SPLIT_USB_DETECT` and forbid `USB_VBUS_PIN`. Neither
selector is read. Absence of a linked `usb_disconnect` symbol is the Source
Gate check (`era_performance_gates.md`).

> **REFUSED:** removing the `is_keyboard_master_impl()` override, or selecting RP2040 split role from raw VBUS (`USB_VBUS_PIN`, or a build without `SPLIT_USB_DETECT`).
> **WHY:** QMK's weak `is_keyboard_master_impl()` in `quantum/split_common/split_util.c` calls `usb_disconnect()` on a judged non-master, and without `SPLIT_USB_DETECT` it reads `usb_vbus_state()`; either change drops D+ at boot or makes VBUS the live role source the moment the override is gone.
> **REOPENS:** that weak override no longer disconnects or reads VBUS, and ERA derives role from something other than the reducer.

| Remote wake | Rule |
| --- | --- |
| Inputs | exactly two local facts in `era_split_keyboard_try_remote_wakeup()` (`split/era_split_keyboard.c`): bus `USB_SUSPENDED`, host granted remote wake |
| Trigger | `era_split_keyboard_note_input_edge()` on the composed-matrix edge. Ungated on host-open, `is_keyboard_master()`, and the action pipeline |
| Consume | a waking press returns false from `era_split_keyboard_process_record()` and is not forwarded as ordinary key input |
| PEER key | reaches that edge only as the HOST's composed-matrix projection; transport receive must not inject HID or push raw USB wake state. That projection exists only while the relation is held (**Relation Hold**) |

> **REFUSED:** a USB remote-wake requester behind the action pipeline.
> **WHY:** the grace close makes `is_keyboard_master()` false, so QMK's `should_process_keypress()` (`quantum/keyboard.c`) holds `process_record()` (`quantum/action.c`) closed for the rest of a sustained suspend, and the requester is then unreachable exactly when the wake is needed.
> **REOPENS:** QMK no longer gates `process_record()` on host-open during suspend, or remote wake no longer consumes a composed-matrix edge.

Every RGB-capable ERA board compiles QMK's RGB sleep capability and has one
persisted **RGB Sleep master** in QMK `keymap_config`: the inverted
`era_rgb_sleep_disabled` bit. Zero/default/CLEAN therefore means enabled. When
the master is off, automatic RGB sleep is off for every reason: explicit USB
suspend, loss of USB frames, and (where present) input-idle timeout. A
TOMAK-family split adds that board-owned input-idle timeout as a third reason.
Split local arm:
`era_split_keyboard_local_sleep_state()` in `split/era_split_keyboard.c` ANDs
the master with the OR of raw QMK `SUSPEND`, `era_usb_session_frames_lost()` in
`system/era_usb_session.c` (`ERA_USB_SESSION_SOF_STALE_MS` 300), and
`last_matrix_activity_elapsed()` against the board timeout.
The timeout is 1..65535 seconds, persisted in TOMAK's two former reserved bytes
inside the syncable eight-byte keyboard config; zero from an older image
normalizes to the 600-second / 10-minute default. The master is not part of that
board record: QMK keymap-config is already its own portable split storage domain,
and remote Apply reloads `keymap_config` immediately.
Model: `era_board_adoption.md` **Non-Split Board Baseline**.
Ownership: **Lighting Sleep Ownership**. Suspend grace and host-open close govern
authority only.

## Lighting Sleep Ownership

**A half's lighting sleep has exactly one owner, and which one is a property of
the relation rather than of the USB stack.**
`era_split_transport_scheduler_lighting_sleep_owner_is_wire()` in
`split/era_split_transport_scheduler.c` is `mode == HOST_PEER_PEER`.
`era_split_keyboard_resolve_lighting_sleep()` in `split/era_split_keyboard.c`
is the resolver.

| Relation role | Owner |
| --- | --- |
| `LOCAL_NO_LINK`, both DUAL-HOST roles, the HOST-PEER **HOST** | its own USB session |
| the HOST-PEER **PEER** | the wire — the relation's HOST |

**The non-owner does not decide the render gate.** A HOST-PEER pair shares the
HOST's resolved sleep fact (including its RGB Sleep master), so on the PEER its local USB and idle-timeout facts are not
inputs. In DUAL-HOST each half keeps its own local timeout and input clock even
when EEPROM sync has ordinarily converged the stored timeout and master bit.

| Edge | Rule |
| --- | --- |
| local → wire (demoted into a PEER) | hold no locally-decided value; resolve **lit**. Dark-until-told is the defect; rotation drops the responder RGB sent shadow, so the HOST answer is due on the first response |
| wire → local (promotion, or becoming DUAL-HOST) | drop the wire's last word; this half's session decides from the next pass |

Device-reported 2026-08-13: a demoted DUAL-HOST→PEER half stayed dark until a
key on the other half — grounds resolve-lit.

The resolver reconciles the physical RGB suspend gate to the current resolved
fact on every refresh; its cached value is only the wire/diagnostic edge shadow.
TOMAK STATUS and lock-indicator presentation no longer clear the suspend gate:
**sleep outranks presentation**. A STATUS edge that occurs while asleep remains
cached and may render after an ordinary wake if it is still active. **A
HOST-PEER HOST publishes the resolved decision and never a transient raw gate.**
DUAL-HOST capture zeroes the sleep bit; apply publishes and does not write the
gate (`era_wire_contract.md`).

On a non-split board, `era_usb_session_task()` applies the frame-loss arm into
QMK's suspend loop. A split board sets `NO_USB_STARTUP_CHECK=yes` in
`split/era_split_qmk_rules.mk`, which deletes that loop, so the resolver above
is the apply.

## Persisted Sync Policy

No DUAL-HOST parent switch and no per-relation EEPROM request. One
relation-independent requested bit per family in
`era_split_sync_policy_set_requested()` (`split/era_split_sync_policy.c`).
Persisting a requested value opens no route, payload, or execution.

| Family | Flag | Scope | Arms | Default |
| --- | ---: | --- | --- | --- |
| EEPROM | `0x01` | the one request replacement storage consumes in every admitted relation | initiator off → no select; responder off → no data, may report `POLICY_CLOSED` | ON |
| RGB | `0x04` | DUAL-HOST RGB runtime: config sections and visual pressed-baseline family, both directions. Effect = `requested &&` eligible (`era_wire_contract.md`). HOST-PEER RGB/visual ungated — `era_split_transport_scheduler_publish_host_peer_responder_rgb_state()` arms the HOST unconditionally; PEER apply consumes the whole body including sleep | DUAL-HOST: sender bit gates capture/arming; receiver bit gates apply (standing visual apply still advances its sequence shadow) | ON since v5 (owner decision 2026-08-13). Off = independently rendering halves |
| INPUT | `0x02` | exactly DUAL-HOST INPUT-class: layer byte and ACTIVITY, both directions. HOST-PEER has no INPUT-class section | RGB pattern, plus: off **sender substitutes the neutral value** (layer 0, zero activity) in `split/era_split_transport_scheduler.c` and `split/scheduler/era_split_transport_scheduler_responder.c`; disable edge clears peer-derived INPUT; enable edge re-observes standing state (`era_split_transport_scheduler_note_sync_policy_edge()`) | ON from v4 |

> **REFUSED:** syncing the split link level through this policy block.
> **WHY:** a sync policy is an owner preference each half may hold differently, but the wire must run one level or no session opens, and putting the stored byte here would gate that pair invariant on a switchable preference and give one byte two writers.
> **REOPENS:** the running level becomes an owner preference that halves may hold differently without opening a session.

The level lives at `ERA_EEPROM_LINK_CONFIG_OFFSET` 200, size 4, sync-excluded.
`split/era_split_link.h` **Reconciliation** meets at Low and raises to the
winner's stored level. Units: `era_source_map.md`. Wire: `era_wire_contract.md`.

Nothing in the protected range `ERA_EEPROM_PROTECTED_CONFIG_OFFSET` 176 /
`ERA_EEPROM_PROTECTED_CONFIG_SIZE` 80 (`176..255`) travels, so an EEPROM clean
is an agreed restart. Both-halves erase:
`era_host_peer_storage_contract.md` **Why An EEPROM Clean Is An Agreed Restart**.
Domain inventory and recency meaning:
`era_host_peer_storage_contract.md`.

Protected neighbors (`storage/era_eeprom_layout.h`):

| Offset | Size | Content |
| ---: | ---: | --- |
| 176 | 24 | local-policy block |
| 200 | 4 | link config |
| 204 | 16 | reset guard |
| 220 | 32 | sync baseline (storage recency) |
| 252 | 4 | protected reserved |

Local-policy block at `ERA_EEPROM_CONFIG_ADDR +
ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET` (`split/era_split_sync_policy.c`
asserts the layout against `era_split_sync_policy_storage_t`):

| Offset | Content |
| --- | --- |
| `176`, size `24` | block; starts the protected range |
| `+0` | `ERA_SPLIT_SYNC_POLICY_STORAGE_SIGNATURE` `0x504E5953`. Change fails the block at boot |
| `+4` | flags generation (any requested change; never 0) |
| `+6` | EEPROM policy generation (EEPROM bit only — INPUT/RGB cannot invalidate an in-flight storage generation match) |
| `+8` | version: **5, the only accepted value** (`ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION` in `split/era_split_sync_storage.h`) |
| `+9` | requested flags EEPROM, INPUT, RGB — mask `0x07`, **all three default on** |
| `+10..+23` | fourteen bytes at `ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET` 10: per-domain 16-bit LE divergence counters, opaque to this module. Ordinary persist writes `ERA_SPLIT_SYNC_POLICY_STORAGE_PREFIX_BYTES` 10 only |

**There is no in-place upgrade. Version 5 is the only accepted value**
(owner decision). `era_split_sync_policy_storage_is_valid()` in
`split/era_split_sync_policy.c` fails an invalid signature, flag mask, zero
generation, or other version. Boot `era_split_sync_policy_init()` then rewrites
the **whole** 24-byte block — defaults, fresh signature, version 5, counters
zeroed. Owner `era_split_sync_policy_reset_to_defaults()` writes the prefix
only; counters survive. Recency degrades conservatively
(`era_host_peer_storage_contract.md` **Arbitration**).

> **REFUSED:** an in-place upgrade of an earlier sync-policy block, or any accepted version other than 5.
> **WHY:** a valid block is loaded verbatim and an ordinary persist writes only the `+0..+9` prefix, so a partial rewrite would leave foreign counter bytes under a valid signature.
> **REOPENS:** a converter that rewrites the whole 24-byte block under a new version, including counters, with a traced class-A fallback.

**A changed default rides the version byte.** A valid block is loaded
verbatim, so a default nothing rewrites reaches only never-booted hardware.
Version 4 carried INPUT's default-on; version 5 carries RGB's.

## Replacement Storage Authority

Domain, transaction, admission, match set, and durable apply:
`era_host_peer_storage_contract.md`. This file owns the direction as a
projection of the relation.

| Rule | Contract |
| --- | --- |
| Direction | per-domain latest-change-wins (`era_host_peer_storage_contract.md` **Arbitration**). Arbitration picks content, never the initiator |
| Initiation | the relation's initiator only — PEER in HOST-PEER, Left in DUAL-HOST (**Initiator Authority**) — for pull, push, and arbitration. Responder data/ack only in the admitted slot |
| VIA toggle | each half keeps its EEPROM requested bit and generation above. Initiator off → no select. Responder off → no data |
| Identity | one engine. A relation change rotates the lane's generation/authority/state/diagnostics |
| `bulk_page_supported` | `true` iff `ERA_HOST_PEER_STORAGE_V1_ENABLE`. Grants no route and no responder independent-send |

> **REFUSED:** a fixed HOST-source / PEER-target winner for replacement storage.
> **WHY:** in DUAL-HOST both halves enumerate and either can take a VIA edit, so a fixed winner discards whichever half the user did not edit second; HOST-PEER steady state is unaffected — a PEER takes no VIA edits — but divergent-history reopens would revert newer content.
> **REOPENS:** DUAL-HOST storage admits edits from only one half.

## Matrix Ready

`era_split_scheduler_session_note_local_facts()` sets
`SESSION_STATUS.matrix_ready` / AUTHORITY `matrix_ready` to
`accepted_no_host && era_matrix_engine_local_matrix_ready()`
(`system/era_matrix_engine.h`). Wire and session refuse `matrix_ready` without
`accepted_no_host` (`split/era_split_wire_payload.c`,
`split/era_split_scheduler_session.c`).

| `matrix_ready` | When |
| --- | --- |
| true only when | local session valid; `accepted_no_host=true`; HOST-PEER fast-path local matrix publisher has a publishable snapshot |
| false for | HOST-open; DUAL-HOST; invalid or unknown local authority |

## HOST-PEER Matrix Admission

`era_split_scheduler_session_host_peer_host_matrix_admitted()` in
`split/era_split_scheduler_session.c` is the session half: local session valid;
peer session known; peer `accepted_host_open=false`; peer
`accepted_no_host=true`; peer `matrix_ready=true`. The role term is the
caller's: `mode == ERA_SPLIT_MODE_HOST_PEER_HOST` in
`split/scheduler/era_split_transport_scheduler_responder.c`. Payload admission
adds `!era_host_peer_storage_route_exclusive()` (`era_route_contract.md`).

**The role term is the relation.** A suspended HOST is still its relation's
HOST and still owns the projection its remote wake reads; closed host-open
governs HID emit, which QMK's suspended `should_process_keypress()`
(`quantum/keyboard.c`) already holds.

> **REFUSED:** a second copy of the HOST-PEER role term derived from `accepted_host_open`.
> **WHY:** **Relation Hold** lets the relation outlive host-open, so asking host-open here folds "am I this relation's HOST" into "may I type right now", and only the second is what a sustained suspend answers.
> **REOPENS:** the relation can no longer outlive local host-open.

## HOST-PEER HOST Source Response Admission

Response-slot only, once a concrete section opens: local relation is HOST-PEER
HOST; the PEER request already passed HOST-PEER heartbeat/source-push
admission; response ACK sequence matches the admitted PEER request. No opened
HOST-source section → one-byte `HOST_PEER_ACK_STATUS`. HOST independent send
closed (`era_closed_surface_contract.md`). Carrier:
`era_route_contract.md` **One carrier for the response section set**.
Eligibility: `era_wire_contract.md`.

## Relation Hold

**Both halves reporting no host is the absence of the fact that assigns the
roles, not a fact that unassigns them.**
`era_split_mode_planner_next_mode()` in `split/era_split_mode_planner.c`
returns `current_mode` for local-no-host + peer-no-host. Naming rules derive
the pair from a half that owns a USB session; with none, the current relation
is held. A PEER with no host of its own does not become HOST because the HOST's
host went away. Ends on enumerate or peer stale.

| Authority pair | Liveness | Next relation |
| --- | --- | --- |
| local-open + peer-open | live | DUAL-HOST (Left / Right by side) |
| local-open + peer-no-host | live | HOST-PEER HOST |
| local-no-host + peer-open | live | HOST-PEER PEER |
| local-no-host + peer-no-host | live | **hold** current relation |
| any | stale / unknown | reset: `LOCAL_NO_LINK` / peer-unknown |

Assignment, hold, and staleness are one transition function. Downstream facts —
**Initiator Authority** first — project that one result.

A held relation keeps its standing exchange (periods: `era_route_contract.md`)
indefinitely if the host never resumes. PEER-key wake fires on state only that
exchange produces. A ≥`ERA_SPLIT_RESPONDER_SILENCE_MS` 100 wire outage, or a
core1 death, inside a held span costs the rest of that suspend its PEER-key
wake (`LOCAL_NO_LINK` hold). HOST-key wake survives on the local composed edge.

A sustained suspend is the ordinary way in: the grace close no longer takes
the relation with it. Forced peer-session stale recovery (**Revalidation
Authority**) and matrix admission (**HOST-PEER Matrix Admission**) are the
same rule at two other sites; re-deriving any from `accepted_host_open` reopens
this section. Device-reported 2026-08-13: HOST key wakes, PEER key does not —
the teardown that the hold closed.

## Initiator Authority

**The initiator is a projection of the relation and not a second derivation of
it.** `era_split_mode_planner_wire_initiator()` in
`split/era_split_mode_planner.c` decides it with the relation, from the
relation, in one plan. Against a held relation a both-no-host `local_left`
arm would hand the wire to a Left HOST still being pushed to.

| Relation state | Initiator | Responder | Matrix route |
| --- | --- | --- | --- |
| Peer unknown | Left only | Right | `SESSION_STATUS` only |
| HOST-PEER | PEER | HOST | PEER source-push only |
| DUAL-HOST | Left | Right | forbidden |
| Right standalone/no peer | none | optional responder | none |

Matrix column = matrix route only. Runtime uses the same assignment. A known
peer whose status is neither host-open nor no-host is not a relation to
project from: this half stays responder. Wire availability (authority
classified, core1 launch not capped) is ANDed at the scheduler, not here.
Sections: `era_wire_contract.md`.

## Revalidation Authority

| Rule | Contract |
| --- | --- |
| `SESSION_STATUS` | discovery, bootstrap, recovery only. Not a sync child. Role and policy/generation revalidation ride each relation's AUTHORITY section (`era_wire_contract.md`) |
| In-relation authority edge | `era_split_mode_planner_decide()` raises no `local_status_required` when `relation_authority_lane_live` is true. That flag is `era_split_transport_scheduler_relation_lane_live()` in `split/scheduler/era_split_transport_scheduler_timing.c`: HOST-PEER PEER (once `matrix_ready`) or DUAL-HOST Left, with AUTHORITY eligible in both directions |
| Two edges keep the frame | a relation that **changed**, and **staleness**. Peer-unknown bootstrap is neither and runs the frame unchanged |
| Both directions | every serviced relation carries AUTHORITY; a responder cannot initiate, so the response direction has no alternative. Body/eligibility/deferral: `era_wire_contract.md` |
| Poll period | `ERA_SPLIT_AUTHORITY_POLL_PERIOD_MS` is defined to `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10. `_Static_assert` in `split/scheduler/era_split_transport_scheduler_internal.h`. Stopped-counter detect 10–20 ms. Any value under the 2048 ms wrap would detect; the derivation stops the period drifting off the window (`split/era_split_authority_reducer.h`) |
| Liveness | heartbeat/source-push liveness carries no session facts; accepted relation traffic may refresh stale detection |
| `local_status_pending` | MUST be cleared by successful `SESSION_STATUS` (`era_split_transport_scheduler_note_attach_status_request_attempt()` in `split/scheduler/era_split_transport_scheduler_routes.c` on `ERA_SPLIT_TRANSACTION_RESULT_OK`), not HOST-PEER ACK alone |
| Failure | fact staleness or payload validation failure → peer-unknown `SESSION_STATUS` bootstrap/backoff |
| Forced stale | `era_split_transport_scheduler_update_mode()` in `split/era_split_transport_scheduler.c`: HOST-open → no-HOST **and the peer is hosted** → force peer-session stale recovery to peer-unknown LEFT-initiator/RIGHT-responder bootstrap. Responder silence also sets stale |

> **REFUSED:** forcing peer-session stale recovery against a peer that is unambiguously no-host.
> **WHY:** that peer was the relation's initiator before the close and still is, and it cannot become the HOST by an edge on this half, so forcing a recovery forgot the peer session and tore down a HOST-PEER relation whose roles were never in doubt, once per computer sleep (**Relation Hold**).
> **REOPENS:** an unambiguous no-host peer can claim the HOST role from a local close.

A peer-unknown or otherwise unusable peer session keeps the forced recovery.

## Authority Scheduler Boundary

| Rule | Contract |
| --- | --- |
| SOF movement | not scheduler dirty state by itself |
| SOF loss / recovery | deadline/due. `era_split_transport_scheduler_sample_authority()` in `split/scheduler/era_split_transport_scheduler_timing.c` is the architecture's one poll (`era_overview.md`) |
| Authority result change | marks `ERA_SPLIT_SCHEDULER_DIRTY_AUTHORITY` for scheduler planning |
| Raw facts | raw authority, SOF, or USB host-state facts must not be pushed to the peer |
| Suspend | SOF loss during USB suspend must not collapse an accepted local HOST relation before the grace window; the three `accepted_no_host` closes are in **Local Authority Facts** |
| Suspend close | USB stack state plus the reducer's bounded suspend duration, not raw VBUS |
| Startup loop | QMK's USB suspended startup loop stays disabled so a no-local-USB PEER keeps running `keyboard_task()` (`quantum/keyboard.c`). `NO_USB_STARTUP_CHECK=yes` in `split/era_split_qmk_rules.mk` |

> **REFUSED:** event-driving the SOF authority poll.
> **WHY:** the SOF counter is a 1 kHz host heartbeat with no loss interrupt, so its absence is observable only by noticing the count stopped; an event-driven poll believes a dead host behind a live cable is still a host.
> **REOPENS:** the USB stack raises a loss interrupt that is the sole observation of a stopped SOF counter.
