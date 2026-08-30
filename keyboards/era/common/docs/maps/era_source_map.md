# ERA Source Map

Status: active
Genre: map
Canonical for: per-file and per-unit source ownership and edit boundaries;
source editing rules; build selectors and where each is declared; the QMK core
modification ledger; stored-data compatibility; the copy-to-RAM policy; the
split/non-split board boundary
Read when: locating an implementation owner, planning source changes, adding or
changing a build option, editing a QMK core file, or adopting a board

## Relation, Policy, And Routing

| Source | Owner |
| --- | --- |
| `era_split_authority_reducer.[ch]` | local USB authority reduction, and the only derivation of local host-open: the `SPLIT_HAND_PIN` side latch and the `is_keyboard_master()`/`is_keyboard_master_impl()` projections QMK reads live here too |
| `era_split_sync_policy.[ch]` | persisted relation-independent sync requested policy facts (EEPROM/INPUT/RGB) |
| `era_split_link.[ch]` | **the split link's rate, and only the rate**: the stored level *and whether a peer ever agreed to it*, the level the wire is running at, the level the owner has picked but not applied, and **Reconciliation** — boot Low, the winner's agreed raise, the Low fallback that writes nothing and the one-shot three-long-pulse report, the listener's recovery ring, and the adoption once a raise is live. Its whole rule is in the header; the wire knows nothing about it. The winner may raise a link request after the standing surface is up; that reopens the 2026-08-19 ruling that this lane raises no request. It owns the level-to-baud map and nothing derived from it — the backend takes the baud and answers the scale, and the scheduler asks the backend for the DUAL-HOST poll period, so there is one derivation of the rate rather than two. The two-phase agreement that used to live here is `era_split_restart_agreement.[ch]`; what stays is the inert-apply test, because the running level is this unit's fact and the service would have to learn what a param means to re-test it. It does not own the PIO: core0 pushes a baud into the backend and the backend never learns where it came from, which is what keeps EEPROM off core1's side of that boundary. Compiled on every split board — the wire boots at Low whether or not a VIA control exists to change the stored rate |
| `era_split_sync_storage.h` | sync-policy EEPROM offset, signature, storage version, and counter-byte layout constants |
| `era_split_mode_planner.[ch]` | relation/mode decision and invalidation requests |
| `era_split_scheduler_session.[ch]` | local/peer session facts and gates, and both carriers that write them: the `SESSION_STATUS` frame and the AUTHORITY wire section. One cache, one change test, two carriers — a second cache is how the two would come to disagree about what a change is |
| `era_split_scheduler_events.h` | lightweight dirty/due producer API |
| `era_split_wire_router.[ch]` | route choice from cached due facts |
| `era_split_transport_scheduler.[ch]` | core0 orchestration, cold storage context, owner transfer, immutable publication/result apply, flash guard, relation rotation, and the performing half of a divider change — the backend teardown-and-rebuild window the listener's recovery step and the agreed raise both use, and the assert that the listener's dwell outlasts two backed-off discovery probes. Also the boot boundary: `..._init()` is policy-only and `..._start_communication_core()` is the single named step that opens the wire at Low (`era_invariants.md`) |
| `scheduler/era_split_transport_scheduler_internal.h` | private scheduler state, and the single definition site for the scheduler cadence macros. A duplicate elsewhere is silently skipped by the include guard and cannot warn |
| `scheduler/era_split_transport_scheduler_timing.[ch]` | authority cadence, route deadlines, liveness, stale detection, storage service bound. Also the sole writer of the next deadline's raw-microsecond stamp, the pre-filter the scan-rate housekeeping entry tests before it reads the clock at all (`era_route_contract.md`) |
| `scheduler/era_split_transport_scheduler_routes.[ch]` | Core1-only general request publication, result drain — lane facts only for the HOST-PEER lane, since the response section set arrives on the standing state — and recovery cancellation |
| `scheduler/era_split_transport_scheduler_responder.c` | core0 live-fact snapshot publication and responder-result apply |
| `scheduler/era_split_transport_scheduler_diagnostics.c` | cached scheduler diagnostic snapshot and baseline reset. **Diagnostics-gated in `era_split_qmk_rules.mk` on `ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE`**, inside the same block as its two siblings in this role rather than beside the unconditional scheduler units: both of its exports are reached only from `era_split_wire_diagnostics.c`. Its declarations in `era_split_transport_scheduler.h` are **deliberately left ungated** — a release-side caller then fails at link instead of silently pulling the whole unit back into the image. Every gated unit's status is legible from the `.mk`, which is the point of gating there rather than inside the file |

Producers set bounded facts only. The scheduler consumer owns planning,
publication, owner transitions, and apply; Core1 alone owns wire IO.

## Wire And Backend

| Source | Owner |
| --- | --- |
| `era_split_wire_protocol.h` | frame marker/length/control-byte constants, payload class and op ids, the wire-framing windows, and the response-section eligibility table that is the authority on membership. Also **the little-endian byte order of every multi-byte wire field**, as the `static inline` accessor set that is its one declaration — get/put at 16, 24 and 32 bits, and the `every` is exact only because the 24-bit pair was added for the one field that had been hand-expanded: byte order is the protocol's and not an encoder's, and this is the header every unit that touches a wire field already reaches. `era_split_wire_frame.h`'s CRC pair keeps external linkage for the opposite reason, stated there. Scheduler cadences belong to `scheduler/era_split_transport_scheduler_internal.h`, not here |
| `era_split_wire_frame.[ch]` | frame CRC8/CRC32, sequence and control-byte construction, compact and bulk-page frame encode/decode. **The CRC32 here is the tree's only reflected CRC32**, declared in the header rather than kept private to the unit, because more than one unit calls it |
| `era_split_matrix_frame.[ch]` | half-matrix bit packing/unpacking and reserved-bit validation |
| `era_split_wire_payload.[ch]` | compact payload classify/validate/encode/decode |
| `era_split_transaction_types.h` | shared result/failure/diagnostic value types |
| `era_split_transaction_backend.[h]`, `era_split_transaction_backend_rp2040.c` | owner-gated RP2040 PIO role lifecycle, send, response-window RX, responder-idle RX, IRQ wake/error. It also owns core1's timer alarm, and therefore the idle wake a core1 actor with a deadline of its own arms: one alarm, one handler, two strictly sequential users, with the safety argument at the declaration. It owns the one park primitive as well — `era_split_transaction_backend_park_until()`, the only place core1 parks inside a transaction in either role: it arms the requested PIO FIFO source(s) and the alarm, parks once on WFE, disarms. Every wait in both roles runs on it — the responder's windows, and the initiator's response window, TX FIFO-full wait and drain — with no selector and no second body, the busy-poll arms having retired. The park instrument (count, microseconds; diagnostic images only) is this unit's, read through the communication-core snapshot |
| `era_split_transaction_io.[ch]` | compact frame and backend primitive bridge; the responder idle receive over a caller-supplied buffer, both frame lanes when the buffer has bulk-page capacity |
| `era_split_transaction_engine.[ch]` | Core1 sequence/control state, transaction orchestration, response validation, diagnostics mirror |
| `era_split_responder_projection.[ch]` | core0's read model of core1's responder: the counters it folds out of already-published Core1 responder results, and no executor. One of its four consumers is not diagnostics — the initiator-silence stale watch in `scheduler/era_split_transport_scheduler_timing.c` reads its request-rx count as a liveness fact |

The backend, IO, and engine expose no unowned/core0 wire entry point. IRQ/WFE
is a wake mechanism; FIFO/error/deadline/cancel/epoch predicates remain receive
authority.

## Communication Core

| Source | Owner |
| --- | --- |
| `communication_core/era_split_communication_core_lifecycle.h`, `*_lifecycle_rp2040.c` | Core1 launch/quiesce/wake, stack, role-directed dispatch, bare IRQ vector. Cooperative only: `era_split_communication_core_request_quiesce()` parks core1, it does not stop it, and the hardware halt deliberately lives outside this unit in `system/era_boot_core1_halt.c` |
| `communication_core/era_split_communication_core_launch_signal.[ch]` | the one-shot user-visible report that core1 never launched: the first-failure latch and the counted-repeat interval table. Timing only — the board owns the painting, and `sirind/common/tomak_common.c` is the only caller wired to it, once for all three tomak boards |
| `communication_core/era_split_communication_core_owner.[ch]` | backend owner role/epoch, revoke/cancel/reset, release/ready handoff |
| `communication_core/era_split_communication_core_initiator.h` | Core1 initiator lane ids and the immutable initiator request/result records shared with the scheduler |
| `communication_core/era_split_communication_core_queue.c` | bounded SPSC general request/result rings |
| `communication_core/era_split_communication_core_host_peer_lanes.c` | the SESSION_STATUS and source-push initiator lanes. A HEARTBEAT lane stood between them with no enqueuer — core1's standing service builds the identical frame from its own period — and retired with the development phase that kept its counters as capture surface. **Every per-lane write in this unit goes through one accessor** over the lane-indexed record in `era_split_communication_core_internal.h`: the three flat `<lane>_<field>` blocks it replaced were one shape written three times, and each consumer of them was a switch choosing between identical arms |
| `communication_core/era_split_communication_core_responder.[ch]` | core0 immutable responder snapshot/result publication and drain |
| `communication_core/era_split_communication_core_standing.[ch]` | the standing exchange, in every serviced relation: the two published records, core0's publish/read side, and the Core1 service that runs the exchange on its own period. Relation-neutral by construction — the plan carries the period and the eligibility, so a new serviced relation is an arm in the plan builder rather than an edit here. Both sides live in one unit deliberately — the records are one contract, and splitting them would let a layout change land on one side only. Core1's sent-state shadow and its stop latch are private here and are not published; the stop latch clears on a *plan* generation change, because the ordinary recovery never rotates the relation |
| `communication_core/era_split_communication_core_responder_internal.h` | private bounded responder storage, and the one shared ring wrap rule both result rings turn over on. It is a `static inline` here because both halves of the pair need it — core0's unit drains, core1's reserves and publishes — and two copies of the rule that decides where an SPSC ring wraps is the shape where a later widening of one ring updates only one of them |
| `communication_core/era_split_communication_core_responder_service.c`, `*_responder_result_policy.h` | Core1 responder admission, reserve-before-response, RX/admitted TX, and the response-section discriminator: a request's own section content decides whether its answer may carry a response section. The policy header is the one hardware-free exception to one-result-per-answer: an exact successful section-bearing HEARTBEAT already represented by the same immutable owner/relation/snapshot/mask reuses that pending Core0 sent-shadow work; SESSION, both push kinds and failure/mask/generation changes never coalesce. Physical duplicate traffic is bulk-folded rather than mislabeled quiet. The service also owns the two free-running counters core0 reads as wire facts — accepted frames, which feeds the responder-silence watch, and undecodable arrivals, which feeds the link listener's step (`era_split_link.c`) |
| `communication_core/era_split_communication_core_storage.[ch]` | storage semantic records, capacity, codec, publication, source-bound replay, lane diagnostics, and Core0's nonterminal ready-result discard primitive for a result whose relation/runtime semantic owner has disappeared while the Core1 lease survives |
| `communication_core/era_split_communication_core_storage_service.[ch]` | cold Core1 storage request/response executor |
| `communication_core/era_split_communication_core_diagnostics.[ch]` | cached lifecycle/owner/initiator/responder diagnostics — the twelve initiator failure counters among them. **No lane identity is cached here**; the lane identity core0 reads lives on the scheduler side, as `core1_initiator_pending_lane`/`_generation` in `scheduler/era_split_transport_scheduler_internal.h` |
| `communication_core/era_split_communication_core_internal.h` | private runtime state shared by communication-core units |

Storage Core1 units may read only immutable semantic records, the pinned
published image, and dedicated scratch. They do not call live
QMK/VIA/RGB/matrix/USB/HID/EEPROM owners or write EEPROM.

## Replacement Storage

The `host_peer` in these file and symbol names is historical: the lane is
admitted for DUAL-HOST as well, and the rename is declined as churn rather than
a correction. What the names cover is stated once, in
`era_host_peer_storage_contract.md`.

| Source | Owner |
| --- | --- |
| `era_split_eeprom_sync.[ch]` | replacement domain/op/status ids, the single request-to-response operation mapping both cores share, and the core0 status/dirty/reload facade |
| `era_host_peer_storage.[ch]`, `era_host_peer_storage_recency_policy.h`, `era_host_peer_storage_indicator_policy.h` | seven-domain manifest, immutable capture, CRC/revision and quiet/due facts, the pull/push wire state machines, latest-change-wins arbitration and its cell queues, and replacement Apply on both roles. Apply validates and revalidates up to ADMIT, then calls the ERA result-bearing remote replacement once; after durable success it reloads runtime state and explicitly publishes manifest/State Sync/convergence. Relation change after ADMIT repairs forward from whichever NVM state won and never owns an EEPROM rollback. It also owns the EEPROM SYNC pending composition: durable local commits feed dirty/recency normally, while an open ERA NVM macro transaction is a display-only unfinished-work arm from the opener until durable close and never a semantic revision. Its persisted-recency owner keeps a missing baseline conservative, makes settled capture publication conditional on a durable counter update, retries failed boot repairs behind the dirty admission gate, and publishes each convergence baseline+counter clear as one storage-metadata NVM transaction before retiring that domain's changed shadow. The two small policy headers are production/test seams: one makes recency publish/retire predicates deterministic; the other owns indicator relation continuity and the role-neutral wire-confirmed sent-level term — serviced relation or finite fast recovery preserves pair presentation, backed-off no-link retires it, and a successfully sent pending one keeps this panel pending until the active carrier confirms zero (`STORAGE_PENDING` on an initiator, `STORAGE_NEWS` bit7 on a responder), while an unsent one creates no synthetic hold. The runtime additionally owns relation admission/departure: if no matching HOST/PEER semantic owner survives a transition, it terminally discards any ready Core1 storage result before an unserviced early return, without closing the reusable source publication slots. The cause profile adds a four-boundary `STORAGE_PENDING` path probe (`plan publish -> Core1 TX -> responder Core1 RX -> Core0 mirror apply`); its Core1 stages use only the RP2040 hardware timer and stage-local bytes, so the selector-only evidence adds no ChibiOS call or cross-core RMW to the measured path and no release wire phase. The unit also owns recovery, diagnostics, and the cached request-pending route-admission fact |
| `storage/era_nvm.[ch]`, `storage/era_nvm_format.h` | production ERA NVM engine and physical format: one 24-KiB public RAM image, two 64-KiB banks, mount/replay authority, verified append/rotation/format, result-bearing range replacement, QMK macro transcript recognition, CLEAN replay proof and bounded inactive-bank maintenance |
| `storage/era_nvm_rp2040.[ch]` | ERA NVM's RP2040 NOR binding for the linker-reserved 128-KiB region; 256-byte page program and verified 4-KiB sector erase primitives, with no QMK wear-leveling dependency |
| `storage/era_eeprom_driver.[ch]` | QMK `EEPROM_DRIVER=custom` adapter: mount/init, stock public reads/writes, exact successful local committed-span publication, `eeprom_driver_erase()`/format, result-bearing REMOTE_APPLY/CLEAN/internal-storage-metadata writes, NVM diagnostics, and one-sector top-level maintenance. Internal storage metadata is durable but intentionally bypasses local semantic/dirty notification |
| `storage/era_eeprom_layout.h` | logical ERA EEPROM range ownership |
| `storage/era_storage_layout.h` | ERA-owned logical address formulas for QMK/VIA portable storage domains; this is the address-classification boundary that lets State Sync/storage stay out of QMK private `nvm_*` accessors |
| `storage/era_eeprom_config_io.[ch]` | bounded core0 NVM bridge for ERA config |
| `storage/era_eeprom_storage.h` | header-only storage facade |
| `storage/era_storage_adoption_rules.mk`, `storage/era_storage_adoption.h` | the storage adoption bundle: selects QMK custom EEPROM, fixes the 24-KiB logical size and 16-KiB macro domain, force-includes the ERA logical layout, and marks a board eligible for EEPROM sync — see `era_board_adoption.md` |
| `keyboards/era/sirind/common/tomak_common.[ch]` | the tomak family's **whole** board content, for `tomak`, `tomak79h` and `tomak79s`: the indicator configuration and its persisted record, the ERA reset guard and strict reset, the seven-domain EEPROM-sync reload table, the VIA value surface, the family keycode enum, the STATUS field's three producers and every QMK hook the family owns. **None of the three boards keeps a `.c`** — what differs between them is geometry, which `keyboard.json` and `config.h` already declare, so the odessey shape applies unchanged. **The header's own preamble is the authority on the family boundary** and on what the unification cost. The STATUS frame's three producers are the core1 launch report, the link-fallback report, and the storage indicator, all advanced from the housekeeping cadence and cached, and the policy pass arbitrates: **the launch report outranks the fallback report, which outranks the indicator**, because a one-shot that a steady ON swallows is invisible for good, while the indicator is the recoverable one and returns on the next pass. Every producer edge invalidates the board policy and calls `rgb_matrix_render_policy_request_refresh()`, so an idle RGB state observes the new STATUS policy on its next task pass and a frame already being flushed is followed immediately by the refreshed policy frame. The fallback pump is withheld while the launch report is live, so the two one-shots cannot share the field. The dirty test is the frame's own bookkeeping — what the last flush actually put on the LEDs — and the producer is deliberately not part of it, in either direction: a handover with the field unchanged needs no re-render, and any non-STATUS push drops the held-frame proof whichever producer held it |

Exact portable domains, exclusions, ranges, sizes, and reload actions are
canonical only in `era_host_peer_storage_contract.md`.

Source owners:

- ERA feature/config modules own the portable ERA config subranges.
- `quantum/dynamic_keymap.c` and stock
  `quantum/nvm/eeprom/nvm_dynamic_keymap.c` own the public dynamic-keymap/macro
  API. ERA NVM interprets the existing final-byte marker below QMK Core: a
  nonzero marker opens staging, payload updates the one public RAM image, and
  the trailing zero commits the complete 16-KiB candidate once. The same NVM
  adapter recognizes stock QMK macro RESET's exact sequential 16-byte update
  transcript; the source-level dependency is pinned by `tests/era_nvm_qmk_driver`.
- QMK eeconfig owners provide RGB Matrix, keymap config, and default layer.
- QMK VIA owns layout options.
- policy, authority, USB/side, protected epoch/reset, VIA magic, and ERA NVM
  physical metadata remain local and excluded.

Storage capture/CRC, route enqueue, result drain, and apply run only from the
cold core0 boundary. Matrix scan may consume only a cached scalar
storage-active fact.

### Storage Adoption

The bundle a board includes to become eligible for EEPROM sync, its five
preconditions and what catches each, are canonical in
`era_board_adoption.md`. What binds here is the refusal:
`era_split_qmk_rules.mk` declines `ERA_SPLIT_EEPROM_SYNC_ENABLE = yes` on
a board that has not taken the bundle, before any compile.

## HOST-PEER Matrix And Response State

| Source | Owner |
| --- | --- |
| `era_host_peer_matrix_link.[ch]` | packed source-push TX/ACK counters and matrix-result apply, and **the PEER's key-path span** — core0 publishing a source-push to core0 applying its ACK, which contains core1's pickup, the wire exchange and the HOST responder's turnaround. It lives here because this unit already owns both ends of that span, and it takes no selector of its own because it stamps per key event rather than per pass: a few hundred a second against forty thousand passes, so the instrument cannot distort what it measures. **It is the only figure this tree has that says anything about the half whose keys do not travel on core0**, which is every HOST-PEER PEER. What it counts is the traffic — heartbeat TX, source-push TX and ACK, and the peer cache's update/project/flush — which is capture surface a `wire hp` line reads. It carries no capture-side publish/change counters: the engine's diagnostics record, this snapshot and the scheduler snapshot each held a copy of the same pair and no printer read any of them |
| `era_host_peer_transaction.h` | shared semantic request/result types |
| `era_host_peer_responder.c` | core0 response snapshot planning and sent-state commit for the responder's **whole** section set — all eight response sections, plus the staged snapshots the visual and RGB ones carry. The set is deliberately not enumerated here: an enumeration re-drifts at the next opened section, and the eligibility table in `era_split_wire_protocol.h` is the authority on membership. Also the send-side projected-length arms, one per planned section — the only sender-side bound against overrunning core1's response payload, so a session sent here to remove a dead bound is in the wrong unit |
| `era_host_peer_response.c` | HSRSP encode/decode, the per-section appliers, and the time-anchor apply watch |
| `era_host_peer_source_snapshot.[ch]` | core0 visual/RGB source staging |
| `era_split_tap_activity.[ch]` | the cross-half tap-hold family, core0 side: the judgment window, the local activity record, the ACTIVITY section's advertised composition, the peer cache the `quantum/action_tapping.c` seam consumes, the family's counters, and the speculative counter sink. Reached from QMK core through that seam's two calls, whose contract is stated once in that file's `era_qmk_fork_ledger.md` row. The behavior is this unit's, not the core file's |
| `era_split_peer_layer.[ch]` | the DUAL-HOST peer's layer contribution: the core0-owned byte, the accessor `quantum/action_layer.c` composes, and the clear that runs whenever the relation is not a confirmed DUAL-HOST. It is the only unit that needs `action_layer.h`, which is why the one-byte width assert for the INPUT layer wire section lives here rather than at the wire definition |
| `system/era_matrix_debounce_config.[ch]` | EEPROM/VIA debounce control to runtime bridge |
| `system/era_matrix_debounce_runtime.[ch]` | scan-bound debounce runtime |
| `system/era_matrix_engine.h` | the split relation's view of the matrix engine, and only that: local row copy, snapshot publish, matrix-ready, source-push due/copy/accept, peer snapshot accept, peer projection sync and scan-idle, relation flush, and the diagnostics snapshot. **A declaration earns a place here by having a caller outside `era_rp2040_matrix_core.c`**, a rule the header states at the block. The engine's own peer-row bookkeeping — apply and clear peer rows, the peer-cache dirty test, and the seq-gated peer matrix copy — is declared in that unit instead |
| `system/era_rp2040_matrix.h` | the raw backend contract the engine reads its rows through — pin preparation, the one-frame read, and the sampler's diagnostics snapshot — implemented by `era_rp2040_matrix_pio.c` below and read by the two split diagnostics units for the snapshot. **It has exactly one implementation and is not an abstraction over two**; why there is no second, and no selector offering one, is `era_build_options.md`'s |
| `system/era_rp2040_matrix_pio.c` | the PIO+DMA raw backend: a PIO1 state machine drives, settles and reads every row on its own, a DMA channel feeds it row patterns from a ring and another carries its samples into a ring, and core0's share is one frame fetch per pass — copy the newest complete frame's words, decode them through byte-indexed tables into `raw_rows[]`, report what changed. Owns the sampler's diagnostics (frame count from the DMA transfer count, torn re-reads, DMA re-triggers, the state machine's stall flags). PIO1 is this unit's alone: it resets the block at init, which is also what makes init idempotent against a previous instance a core-only reset left running |
| `system/era_rp2040_matrix_pio_frame.h` | the sampler's hardware-free half — instruction encodings, the settle/release program encoder, pattern and decode-table builders, the ring-frame arithmetic — so `tests/era_rp2040_matrix_pio` proves it on the host without a device |
| `system/era_rp2040_matrix_core.c` | full matrix engine and QMK-compatible `matrix_*()` surface; its scan is one frame fetch per pass through the raw backend contract, and it defines none of QMK's three matrix delay hooks — under `CUSTOM_MATRIX` nothing compiled calls them, and the one caller in the tree (`split_util.c`, behind `SPLIT_HAND_MATRIX_GRID`) would fail the link by name rather than silently |

The matrix engine owns raw scan, debounce, local/composed rows, accepted peer
cache/projection, and changed-state publication. Scheduler and wire code own
relation, admission, freshness, and route priority. QMK action/key processing
remains outside the engine.

### Board Adoption

The copy-to-RAM policy, the non-split capability boundary and baseline,
and the adoption checklist are canonical in `era_board_adoption.md`.
They lived here under a heading about the HOST-PEER matrix, which is
where they landed rather than where they belong.

## Board Integration And Entry Points

| Source | Owner |
| --- | --- |
| `system/era_board_hooks.[ch]` | the board-facing extension contract **both** class skeletons call, and its weak defaults. "Both classes call every hook here" is a checkable promise rather than a comment, and it is checkable because the points only one class calls live beside that class: `system/era_nonsplit_board.h` holds the config load/reset pair, `split/era_split_board.h` the pre/post-init pair. A hook reaches this file by surviving that split. The rule it enforces is that a hook declared for one class and silently never called on the other is a defect generator — which this file itself carried for four commits, with the config pair declared here and called only by the non-split skeleton. The defaults are one unit rather than one copy per skeleton, because two units sharing a header and duplicating the same bodies is the under-split shape **Source Editing Rules** names. A hook earns a place here only by being one the skeleton takes a *strong* definition of — QMK's own weak hooks (`rgb_matrix_indicators_kb`, `keyboard_post_init_kb`, `led_update_kb`) stay a board's to define directly, or a **common feature's** where one exists and the board turned it on: `features/era_rgb_indicator.c` takes three of them under `ERA_RGB_INDICATOR_ENABLE`, which is a mutual exclusion with the board rather than a second route to this file. The skeletons reach the housekeeping hook through `era_board_housekeeping_tick()`, which forwards at most once per millisecond: everything boards hang off this cadence is millisecond-scale by contract, and re-asking it every scan pass was part of a measured scan-rate regression. **The keyboard channel's quiet persistence gate is here for exactly the intersection of those two facts** — this is the one unit that sees every channel-0 command *and* owns that cadence, so `id_custom_save` **schedules** the channel's write `ERA_STORAGE_QUIET_DEFER_MS` after the last save rather than performing it, and the commit costs one byte load and a compare per millisecond and nothing per scan pass. It runs `era_common_via_keyboard_channel_save()` and `era_board_via_save()`, which is the whole of what it covers; tap dance stays immediate at the router's own save arm. `era_board_persistence_flush_pending()` beside it commits every gate holding an approved write and writes nothing for a gate holding none, for the two windows that would otherwise lose or poison one — this unit's strong `shutdown_kb()`, the only QMK hook here that is not an `era_board_hooks.h` default, and the non-split suspend hook in `system/era_usb_session.c`. It is also the **one writer of `id_unhandled`** in the ERA VIA surface: both skeletons call `era_board_via_keyboard_channel_command()` here unconditionally as their last statement, so every command no handler claimed arrives at it. The rule that keeps that single-writer property true — *a handler returns true only for a command it answered, and writes nothing otherwise* — is stated at `system/era_common_via.c`'s router, and it is load-bearing rather than tidy because two channels carry a second claimant behind the first |
| `split/era_split_board.[ch]` | the **split** class skeleton: `housekeeping_task_kb`, `keyboard_pre_init_kb`, `keyboard_post_init_kb`, `suspend_wakeup_init_kb`, `process_record_kb` and the one `via_custom_value_command_kb`, over `era_split_keyboard.c`. Owning `housekeeping_task_kb` buys the split class the same double-tap guarantee the non-split skeleton buys, because `era_split_keyboard_task()` calls `era_common_features_task()` first. The housekeeping order is now explicit presentation priority: split runtime/scheduler first, then the board's millisecond presentation tick, then `era_common_features_maintenance_task()`; the last yields while an RGB policy refresh is unfinished, so opportunistic bank erase cannot stretch one half's STATUS edge. Its header adds **two** extension points to the class-neutral set, split-named and split-scoped: a split board's post-init content sits between `keyboard_post_init_user()` and `era_split_keyboard_post_init()`, before the wire opens, where the one non-split board with post-init content needs to run earlier — one shared hook could not hold both positions without moving a board's code. Declaring them in a split-only header is what keeps that from becoming the silent-no-op hazard the neutral set is shaped to avoid. **It deliberately does not own the init trio**: on the only split family in the tree the ERA module init runs *conditionally* inside `matrix_init_kb`, and a class skeleton calling it unconditionally would add an EEPROM re-read on every boot — a change rather than a move |
| `system/era_nonsplit_board.[ch]` | the **non-split** class skeleton: `housekeeping_task_kb`, `matrix_init_kb`, `eeconfig_init_kb`, `process_record_kb`, `via_init_kb` and the one `via_custom_value_command_kb`, over `era_common_features.c` and `era_common_via.c`. Its housekeeping uses the same common priority order as split — feature task, board tick, opportunistic NVM maintenance — so the maintenance contract is class-neutral. Its header adds the config load/reset pair to the class-neutral set, class-scoped for the reason the neutral header states: the split skeleton calls neither, because it does not own the init trio. Class-scoped by content as well as by name — it may reach `era_common_*` and nothing under `common/split`. Owning `housekeeping_task_kb` is what retires the double-tap closer hazard (`era_board_adoption.md`), which is the reason this unit exists rather than the deduplication |
| `era_split_transport.c` | QMK `split_common` transport hook adapter over the ERA scheduler; carries no policy and triggers no core1 launch. Its whole live surface is the two init hooks `split_util.c` calls — `transport_master_init`/`transport_slave_init`, both reaching `era_split_transport_scheduler_init()` — because the row-array step pair and `transport_execute_transaction` have no compiled caller (`CUSTOM_MATRIX=yes` and `SPLIT_TRANSPORT=custom` exclude `quantum/matrix.c` and `quantum/split_common/transactions.c`) and are deliberately absent: a build that re-adds either caller fails at link by name instead of running a stub. The scan path's transport step is the scheduler's own `era_split_transport_scheduler_transport_step()`, called from `matrix_post_scan()` with no role branch |
| `era_split_keyboard.[ch]` | ERA split facade over the QMK keyboard hooks: pre/post init, task, feature reload, USB device-state change, the sleep decision and its RGB Matrix apply, the composed-input-edge remote wake, process-record, VIA command entry. Also the agreed restart's composition root — the one unit that knows both of its users — where the act table and the act dispatch are defined side by side (`era_split_restart_agreement.h` declares both). Owns the two boot instants that matter — pre-init runs USB identity and authority-reducer init, post-init runs storage init and then the single core1 launch step. **A split board runs QMK's whole wake path and none of its sleep path**, and that asymmetry is load-bearing: `suspend_power_down_kb` would reach a split board only through QMK's suspend loop, which `NO_USB_STARTUP_CHECK` deletes, so there is no `suspend_power_down` chain here at all — while `suspend_wakeup_init_kb` *is* live, reached from `usb_main.c`'s wake handler outside that guard |
| `era_split_via_link.[ch]` | VIA command handling for the split link value ids, on the same channel and behind the same declining dispatch as the sync unit below. It carries the `#error` that refuses a build whose link speed the SYSTEM page's labels were not written for. USB re-enumeration is armed from the owner Apply commit (`era_split_via_link_schedule_reattach()`), run from `era_split_via_link_task()` after the scheduler so it cannot occupy T_commit; the quiet gate is `era_via_system_restart_quiet_ok()`. An inert, refused, or expired Apply does not bounce |
| `era_split_restart_agreement.[ch]` | **the agreed restart: both halves of a pair commit the same act at one instant.** One mechanism, two users, and it knows neither — a user hands over an `(act, param)` and this unit runs the quiet gate, the two-phase agreement, the shared-clock deadline and the local path when no relation is serviced. What an act *is* — four properties: whether the initiator may commit without the responder's answer, whether the arm waits out a storage episode, whether the service resets after prepare, and how wide the act's param may be on the wire — and what an act *does* are declared here by name (`era_split_restart_act_rules[]`, `era_split_restart_prepare_local()`, `era_split_restart_arm_ready()`) and defined in `era_split_keyboard.c`, so this unit includes no user's header and names no user's constant. The link act does not reset; CLEAN does, and therefore requires the responder's matching arm while a relation is serviced. The tie between two simultaneous requests is Left, enforced by the initiator with no clock in it: a Left initiator takes its own request first, a Right initiator arms the peer's when one is visible and its own otherwise. A commit retires this half's leftover request and will not arm a peer request until that peer has advertised idle, because the link act does not reset and a leftover Apply would otherwise become the next arm. The request rides the AUTHORITY flags byte and the arm rides the `RESTART_ARM` push section, for the reasons in `era_wire_contract.md` |
| `era_split_via_sync.[ch]` | VIA command handling for the sync-policy value ids. They sit on `ERA_VIA_SYSTEM_CHANNEL` **behind** `era_via_system.c`'s own value ids on that same channel, and are reachable only because that unit declines what it does not answer — the concrete case the unclaimed-answer rule at `system/era_common_via.c` exists for |
| `era_split_usb_identity.[ch]` | side-dependent USB identity selection at init. **A split half advertises the product id in its board `config.h`'s `ERA_SPLIT_USB_IDENTITY_PID_LEFT`/`_RIGHT`, not `keyboard.json`'s `usb.pid`** — so the schema key names a value neither half actually presents, and a tool that reads the JSON to identify a connected half identifies the wrong thing |
| `system/era_boot_core1_halt.c` | the whole pre-copy window: a strong `early_hardware_init_pre()` override, pinned to a flash VMA by the `.flash_startup` carve-out. crt0 is its only caller. It has two permanent unrelated jobs, the core1 hardware halt and the double-tap bootloader arm, because the hardware offers one call on that side of the copy loops and both need to be there; the file name predates the second and a second pinned object was judged to cost more than it is worth (reasoning at the top of the file). The production path classifies the exact physical-RUN joint reset tuple, writes the scratch0 survival marker, and holds a first qualified RUN clear through 768 bounded 240-tick ROSC `COUNT` chunks before writing ARMED. Editing it means reading the carve-out rules in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` first, because it may not spin unbounded, call into `.sram_image`, or touch anything but the sole four-byte no-init exception and reset-domain MMIO in `era_invariants.md` |
| `system/era_vector_defaults.c` | strong SRAM definitions for the vector slots ChibiOS leaves at its weak default, aliased onto one `era_unhandled_vector`. Owns no state and has no ERA caller — the linker consumes it, and the table entries are the product. Editing it means reading the derivation note at the top: the enumeration is exactly the set that resolved to the weak default at the time, so a driver that starts installing one fails the link on a duplicate symbol, and one that stops installing a handler fails the `.vectors` gate. **Answer either with the driver's own condition, never by deleting the entry** — five slots are already written that way (PWM, the two PIO, the two DMA), and deleting one fixes the board that has the driver while silently returning the slot to flash on every board that does not |
| `system/era_usb_session.[ch]` | **the local USB session as ERA reads it, and the only owner of each fact in it.** The unconfigured-SUSPEND-to-INIT remap; the single RP frame-counter sampler and its last-change stamp; the has-a-host-ever latch that keeps a charger from reading as a sleeping host; the frame-loss arm of the sleep decision; the firmware-reattach hold a VIA Apply bounce asks so the bus-down window is not a HOST close and not lighting sleep; and — non-split only — the apply plus the strong `suspend_power_down_kb`/`suspend_wakeup_init_kb` pair that tracks what actually happened to the lighting whoever drove it. Three things in it are written down because they are not guessable — the frame arm is a second **detector** of one event and not a second signal; `USB->SOFRD` is a read-with-side-effect register and the sampler's stand-off guard exists for that reason — while the once-per-millisecond pacing is also priced: the unit keeps time in raw microseconds (`timer_hw->timerawl`) because platform timer conversions at the scan rate were a measured scan-rate regression (**Non-Split Board Baseline** carries the hardware fact); and `era_usb_session_sample_frame_age()` reports an age **and** an availability because its two consumers need opposite answers when no sampler exists |
| `system/era_via_system.[ch]` | ERA system VIA command router and its task. Owns the EEPROM CLEAN three-value confirm mask, its 10 s window from the first bit, the clear-on-zero cancel, **the one restart-quiet policy** (500 ms silence after the last RAW OUT report, 2 s bound) and the `via_command_kb()` stamp it reads. Every cut this firmware makes into VIA traffic asks `era_via_system_restart_quiet_ok()` — CLEAN, the split link's agreed switch, and the VIA Apply USB re-enumeration — so the constants sit outside every feature guard. It also owns `era_via_system_eeprom_invalidate()`, which submits the one boot-magic word through the physical boot-replay proof before `PREPARED`, and the weak hand-off `split/era_split_keyboard.c` overrides to route a split board's clean through the agreement |
| `newone/common/odessey_common.[ch]` | the odessey family's whole board content, for `odessey60h` and `odessey60s`: the indicator config, its EEPROM record, the HSV cache, the underglow effect range, the VIA value table, and the two QMK weak hooks the class skeleton does not own (`keyboard_post_init_kb`, `led_update_kb`). Family content, not class content — the next non-split ERA board shares none of it, which is the boundary the three tiers draw. The two board files it replaced were string-identical after the board-token substitution, so neither board keeps a `.c`; each `post_rules.mk` states the one `SRC` line, because one `SRC` line is not a build option and has nothing to declare |

## Diagnostics

| Source | Owner |
| --- | --- |
| `era_split_transport_scheduler_diagnostics.h` | diagnostic snapshot value type |
| `diagnostics/era_split_transport_scheduler_role_diagnostics.[ch]` | explicit role-era baselines |
| `diagnostics/era_split_wire_diagnostics.[ch]`, `*_counter.c` | paced wire output and optional scan/raw hooks |
| `diagnostics/era_via_macro_diagnostics.[ch]` | cause-variant-only decomposition of a dynamic-macro RAW-HID exchange: actual set-buffer request arrival, handler time, response-queue admission, endpoint drain and the application-side interval after drain. `quantum/via.c` supplies the request/response brackets; `era_split_keyboard_task()` polls QMK's RAW IN endpoint after `raw_hid_task()`, and `WIRE_DIAG` owns snapshot/reset. No release image compiles the unit |
| `diagnostics/era_split_qwin_diagnostics.[ch]` | silent qwin counter window and compact result, and with it the window's per-slice scan rates (`seg=`), the deferred start (`settle=`), core1's sleep over the window (`park=`) and the PIO sampler's frame rate and counters — timed from a once-per-millisecond tick `era_split_keyboard.c`'s paced block calls, so the scan path carries nothing new. It is also the printer for the pass-phase instrument below, which is the one thing on this line that does cost the scan path and is why that instrument has a selector and a rung of its own |
| `system/era_pass_phase_diagnostics.[ch]` | the pass itemised: twelve contiguous segments tiling one `keyboard_task()` iteration, their accumulators, and the marks that close them, **and the per-segment and whole-pass maxima beside them**. Common-layer rather than split because the pass is every board's, printed by the split qwin unit because that is where a window exists to print it into. It owns the reason it is not a rider on the diagnostics selectors: twelve raw counter reads a pass cost scan rate, so folding it into `qwin` would move the comparison point every reading is judged against, and the `qwin_phase` rung exists so the price is measured instead. **The maxima are the half that answers where rendering should run** — render is about eight working passes per 16 ms frame among six hundred idle ones, so an accumulator structurally cannot see it and a maximum can; they are absolutes cleared at the window's start rather than deltas, because two snapshots cannot subtract into a worst case. Five of the twelve segments end inside `quantum/keyboard.c` and reach this unit through named entry points, because core cannot see the segment ids (`era_qmk_fork_ledger.md`). **The RGB sub-marks stamp at most one arm a pass, not exactly one**, since the idle gate returns ahead of them — so the four arm counts no longer sum to `ph`, and the difference is the gated passes (`era_capture_reading.md`) |

Formatting and snapshot construction remain outside matrix scan. Only
compile-time count/raw hooks may be scan-bound.

## Build Variants And Host Tooling

| Source | Owner |
| --- | --- |
| `keyboards/era/era_build_identity_options.mk`, `system/era_build_variant_rules.mk`, `common/build_variants/*.mk`, `sirind/brick65/post_rules.mk` | the firmware-inert identity declarations, board-independent validation, exact override-resistant five-axis diagnostic combinations, link-visible compiled witnesses, and compatibility refusals; `standard` is universal while split/storage prerequisites decide which diagnostic variants a target admits. Brick65's post-rules include only this make-time identity layer and the option printer, not ERA firmware |
| `tools/era_qmk_build.sh` | automation-only, explicit-target and explicit-variant QMK clean build, requested-versus-resolved make identity check, resolved-versus-compiled ELF witness check, copy-to-RAM gate, and identity-labelled artifact/manifest capture for every ERA keyboard; it contains no board-name arm and emits no artifact before all three tuples agree |
| `tests/era_build_variant_rules/` | make-time proof of every canonical variant tuple, direct command-line overrides, environment and `MAKEFLAGS` injection including `make -e`, non-split `standard`, non-split diagnostic refusal, and retired profile rejection |
| `tests/era_nvm/` | fault-injection proof of the production A/B format, mount/replay, append ordering, atomic replacement, rotation, format, macro transaction/RESET semantics and inactive-bank maintenance |
| `tests/era_nvm_qmk_driver/` | stock-QMK-facing adapter integration: ordinary EEPROM API, exact local notification, the actual stock macro RESET loop, non-macro write during macro staging, macro-overlap refusal, failed close, whole-store erase and CLEAN physical replay/fault proof |
| `tools/era_qmk_fixed_builddate_wrapper.sh` | explicit fixed-magic test-only `QMK_BUILDDATE` generation override |
| `tests/era_rp2040_matrix_pio/` | the host proof of `system/era_rp2040_matrix_pio_frame.h` (`make test:era_rp2040_matrix_pio`): encodings, the shipped program, patterns for both tomak79h hands, decode tables against the header's per-column reference rule (the retired CPU engine's), the ring arithmetic. The repository's one unit test since the reducer harness retired |
| `tests/era_split_responder_result_policy/` | deterministic host proof that responder-result coalescing is limited to exact successful section-bearing HEARTBEAT sent-shadow duplicates and fails closed for every result class that can carry peer input or a changed identity/mask |
| `tools/era_core1_stack_walk.py` | the ELF gate's core1 stack disassembly walk, read-only, over an ELF or an `arm-none-eabi-objdump -d` dump. **Changing its method invalidates every figure taken with it** — the budget it measures has been read with three different instruments, and only the margin and the chain shape carry across such a change |
| `tools/era_doc_refs.py` | the tree's one read-only check that **every claim an agent reads is locatable**. With no argument it resolves each backticked or linked repository path in every spelling the documents use, each `path:line` citation against the file's length, each routed-to section against the target's headings, each document's `Status:`/`Genre:`/`Canonical for:`/`Read when:` header, the index's naming of every agent document, the file a paragraph owes when it claims what a resolvable function does, and — since 2026-08-17 — **the same path and citation resolution over every comment in `keyboards/era` source**. That last check is the reason the row no longer says "the document layer's": the rule was written for documents and is not about them, and the day it was extended it found six of the seven `path:line` citations in ERA source comments pointing somewhere else against zero findings in the document layer, which is what a checked surface looks like beside an unchecked one. Comments only — an identifier in code is resolved by the compiler and a string literal is data. `--homeless [A..B]` is the deletion safety net over removed doc lines, which is a judgment aid rather than a verdict and so is never armed. **What it deliberately cannot do is tell whether a claim is still true** — a resolvable reference and a true statement are different questions, and only the first is mechanical. It is tool-neutral canon and is named here for that reason: a script reachable only through one tool's configuration is one a second tool cannot discover, which is what AGENTS.md's Agent Layer Ownership rule forbids |

The keyboard rules own common profile defaults. Launchers select a profile but
do not duplicate compiler flags or change firmware policy.

## Load Layout

| Source | Owner |
| --- | --- |
| `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` | common-layer SRAM-resident load image for every ERA board |
| `system/era_sram_resident_rules.mk` | the residency bundle a board includes to become copy-to-RAM: the linker script selection, the `ERA_SRAM_RESIDENT_IMAGE` marker, the map the ELF gate reads, the pre-copy window object, and the vector defaults. One file because the five fail as a set, and an include rather than a variable — see `era_board_adoption.md`'s **Copy-To-RAM Policy** |

There is no QMK placement-bridge layer: `quantum`, `platforms`,
and `lib/lib8tion` files carry no ERA placement machinery (digital_rain
keeps a lib8tion random16 for the no-allocator invariant). Physical
placement is canonical in `era_sram_residency_contract.md`. The QMK core
matrix boundary is canonical in `era_invariants.md`.

## QMK Core Modifications

Retired from this document to `era_qmk_fork_ledger.md`, which is
canonical for the whole core-edit set, its gates, and the re-derivation
against pristine upstream. The rule is unchanged and binds here: **any
ERA edit to a QMK core file must appear in that ledger in the same
change.** A fork edit nobody records is a fork edit nobody can retire.

## Board Feature Owners

- `features/era_{kkuk,socd,tapping,tapdance}.[ch]`: runtime and feature-local
  persistent config; `era_tapdance_introspection.c` supplies the QMK tap-dance
  introspection surface.
- `features/era_mousekey.[ch]`: the persisted image and the six-control adapter
  for QMK's default accelerated mouse engine. **It is the one feature unit with
  no runtime state of its own**, and that is what it is for: every value it
  tunes is already a writable runtime variable in `quantum/mousekey.c`, so the
  unit is an image plus an assignment into those variables at init, at a
  cross-half reload and at each VIA set. The two the engine kept as macros
  become variables under `ERA_MOUSEKEY_RUNTIME_DELTA`
  (`era_qmk_fork_ledger.md`). Its page shows six of the ten stored values and
  stores all ten, and the two cursor speeds it shows are effective pixels per
  event rather than the engine's step and multiplier — the derivation is in
  the unit, the same place `era_tapping_normalize_term()` quantises. **The
  runtime multiplier is derived and not stored**: acceleration off applies
  `mk_max_speed = 1`, which collapses the engine to the start step alone, while
  the stored ratio stays untouched for when a ramp is chosen again. It shipped
  applying the *top* step there and was unusable at all six of its values —
  device 2026-08-18 — because what a user feels is the step size, not the
  average speed, and the wheel's own off level had always been the base step.
  **The climb is held as a duration and the engine's event count derived from
  it**, so the update rate changes only how finely the motion is cut; the unit
  the page states it in is chosen for exact readback rather than resolution,
  and the reason is at that setter. **The block's defaults are ERA's own and
  not upstream's**, from the same sitting: upstream's 400 counts a second
  climbing to 4000 in 0.6 s was tuned for the displays of its day. Which
  controls transfer between users and which one follows screen width is
  written at the top speed's setter.
- `features/era_backlight.[ch]`: the PWM backlight effect layer for the
  boards in that lighting family — which of four effects is selected, the
  breathing period, and the two keypress-reactive blinks. **It reads no clock
  on the scan path**: the blink interval is a ChibiOS one-shot whose callback
  raises one flag, so the per-pass cost is a byte load and a branch in every
  effect. The unit's own header carries why, and the two implementations it
  rejects.
- `features/era_backlight_always_on.[ch]`: the same pin as an *indicator
  supply* — twelve lines that repair a stored backlight-off block, for the
  three boards whose backlight drives nothing but their lock LEDs and which
  therefore ship no lighting surface at all. Its own unit rather than an arm
  inside the file above, because a board takes exactly one of the two and the
  make layer refuses the pair; folding them together would put two claims about
  one pin in one translation unit. **It writes eeconfig and never the
  hardware**, which is what lets it run from the ordinary feature init instead
  of deferring to the first pass: that init is reached before
  `backlight_init()`, and the file says where.
- `features/era_rgb_indicator.[ch]`: the RGB Matrix lock-indicator slots for
  the boards in *that* lighting family — one or two slots, each with a lock
  source, a brightness and a colour, painted on the LED index that board's
  `config.h` names. **It is the one common unit that takes QMK's weak render
  hooks strongly** — `rgb_matrix_indicators_kb`,
  `rgb_matrix_indicators_advanced_kb`, `led_update_kb` and
  `rgb_matrix_render_policy_kb` — so a board with the selector on defines none
  of them, which is what let the last two non-split board `.c` files go. It
  reads no clock either: the lock state and the colour are both edges, and what
  they raise is one dirty byte the render pass reads. Not the tomak or odessey
  indicator, and its header says why folding the three together would buy one
  unit carrying three products' rules.
- `features/era_{backlight,debounce,kkuk,mousekey,rgb_indicator,socd,tapping,tapdance}_via.[ch]`:
  VIA adapters.
- `system/era_common_features.[ch]`: feature init/reload/task facade plus the
  class-neutral opportunistic NVM-maintenance owner. The feature task itself
  never erases; both class skeletons call the maintenance task after the board
  presentation tick, and RGB render-policy refresh keeps that background erase
  yielded until its frame reaches the PWM flush boundary.
- `system/era_common_via.[ch]`: VIA command router.
- `system/era_state_sync.[ch]`: RAM KEYMAP/MACRO/CONFIG revisions and the exact
  `GET_KEYBOARD_VALUE` `0x06` envelope consumed by external H7S work. Increment
  is O(1); the GET path does not read EEPROM, CRC, or snapshots. MACRO revision
  publication is withheld while QMK's dynamic-macro cache transaction is open
  and advances at its trailing successful durable boundary. The envelope and
  H7S adoption boundary are canonical in `era_host_peer_storage_contract.md`;
  H7S does not own this firmware's split authority or Apply mechanism.

## Source Editing Rules

- The discipline every owner above works inside is canonical in
  `era_sram_residency_contract.md`: what a scan path may consume, what must
  stay at a task boundary, and the per-function placement machinery that
  stays closed because the load layout owns placement.
- Inspect actual call graphs and disassembly before changing source/link order.
- **What earns a structural change** (owner decision). Three
  defects, and unreachability is only the first of them:
  - **Unreachable code**, including code reachable only through a preprocessor
    arm the build system makes impossible.
  - **Over-split structure** — a unit that exists as its own translation unit,
    or its own header, without carrying a distinct owner or a distinct reason
    to be separately compiled; a declaration split from its only user; a
    header whose whole content is one unit's private business.
  - **Under-split structure** — a unit carrying two owners, two eras, or two
    vocabularies; a file whose arms no longer share a shape; duplicated logic
    across units that already share a header.

**Length is not a fourth defect**, and that caveat qualifies how a case is
argued rather than whether it may be opened. This repository's comments
deliberately carry retired reasoning, so a file being long is not the finding —
what the length is made of is. Cite the composition, not the total.

**Do not reinstate the bar this replaced** — that a structural change is
justified only by a current ownership or measured executed-work problem, and
files are never split for being long. Applied honestly it refuses almost every
structural change: a structure that is merely wrong is not yet an ownership
*problem*, and nothing measures the executed work of a file boundary.

Finding these three is a scan, and the scan does not live here:

> **REFUSED:** commit the structural detectors that find them — duplicate-window,
> dead-static and struct-shape scanners — beside `era_doc_refs.py`.
> **WHY:** they emit candidates, not violations, so nothing can arm them, and an
> unarmed committed script is a surface to maintain with no failure it can
> report; the half of that work that *does* emit violations — an unresolvable
> reference — was folded into `era_doc_refs.py`, which the commit gate already
> runs.
> **REOPENS:** a detector whose every finding is a fact about the tree, which is
> the same bar `era_doc_refs.py` meets and the reason it is committed.

Two structural changes are refused standing, each for a reason that is not
"it is fine as it is":

> **REFUSED:** split the two large storage units, which really are under-split.
> **WHY:** every cut promotes shared helpers to external linkage and hands the
> compiler a fresh inlining decision on the storage path. Measured over both
> units' call graphs: the widest private subtree in
> `era_host_peer_storage.c` is `..._runtime_task`'s, and lifting it takes 18
> helpers and both file-scope statics with it, while
> `communication_core/era_split_communication_core_storage.c` shares 16 of its
> 17 statics between two or more exports. Precedent for the price:
> `plan_from_snapshot`'s measured `noinline`, 3 → 2 callers costing +216 B of
> image against a 182 B body.
> **REOPENS:** a cut that moves no shared helper across a unit boundary — which
> the figures above say does not exist today rather than has not been looked
> for. Re-derive them from the call graph, not by reading.

> **REFUSED:** delete the defensive `default:` arm on the lane switch in
> `split/communication_core/era_split_communication_core_host_peer_lanes.c` —
> the only one of that unit's four (`git grep -c "default:" --` that path) that
> is defensive.
> **WHY:** the lane crosses the core boundary as a `uint8_t` in an immutable
> record, so what the switch reads is data rather than a type and no compiler
> exhaustiveness check can stand in for the arm.
> **REOPENS:** nothing — permanent while core0 publishes the lane as a byte,
> which the cross-core record requires.

## Build Selectors And Their Dependencies

Retired from this document to `era_build_options.md`, which is canonical
for the declaration rule, every selector, the dependency edges and the
rejected arrangements. What stays here is the source side: which unit
reads an option is a per-file ownership question and belongs to the
tables above.

## Stored-Data Compatibility

**There is none** (owner decision). No ERA code exists to read,
accept, or convert a format some earlier firmware stored. A firmware upload
already clears the keymap by design, the shipped guidance states that as a
fact and points at VIA's SAVE and LOAD in both user guides
(`user/readme.txt`, `user/readme_split.txt`), and an existing
owner's stored data is explicitly not a consideration. Every path that survives
only to accept an older stored layout is a deletion candidate — and the
deletion, not the finding, is where the difficulty is.

**Deleting an old-format acceptance is safe only when the code that runs
instead treats the old block as invalid and writes a fresh one.** Trace the
fallback before deleting and classify it:

- **A. Clean reset** — the old block fails a signature, magic, or prefix check,
  the code writes defaults over it, and the owner sees factory settings. Safe.
- **B. Misread** — the old block passes the surviving validity check and is
  then interpreted under the new layout. Not safe, and not a deletion either:
  it is silent corruption of a configuration, which is a worse outcome than the
  compatibility being retired. It needs a guard bump, not a deletion.
- **C. Undefined** — the fallback cannot be traced. Report it; do not guess.

**Worked example, the link block's 2026-08-19 change** (`split/era_split_link.c`).
One of its three reserved bytes became a flags byte carrying the agreed/unagreed
mark. Traced both directions: an older block reads with the byte at zero, which
is *agreed* — the level itself is read identically and the conservative mark
means a level that fails is dropped to High after one boot rather than
re-raised, so **class A**, and the only cost is that a pre-upgrade pair set
half-by-half converges to High instead of to the chosen level, one Apply away.
A newer block read by an older image fails that image's reserved-zero test and
reads as High, also class A and owed to nobody. **No key bump**, because
nothing is misread under either layout — which is the whole of what the
classification asks.

**Do not skip the trace on the reasoning that nobody can still be holding an
older block — that claim is short, plausible, and wrong.** It runs: a deployed
owner's reset key is older than the compiled one, so the guard fails, the
strict reset zeroes the ERA block, and no real block is ever read under a newer
layout. What it misses is that **the reset key and the block version move
independently**. `ERA_EEPROM_RESET_KEY` changes rarely and the sync-policy
block has been through five versions; every build made between a key change and
a later version bump carries the *current* key over an *older* block, so the
guard passes and the old-version path is live for whoever is holding one. A
firmware upload does not close it either: the build-date magic resets VIA's
region, not the ERA config block. That is why each deletion is classified by
its fallback rather than waived; where the fallback is A the loss is accepted
and bounded — the whole-block rewrite costs three sync toggles (EEPROM, INPUT,
RGB) and the seven divergence counters.

**A version bump is also how a changed default reaches an already-booted
board**, and version 5 exists for that alone: its layout is identical to
version 4's and only the default flag set moved. The rule is canonical in
`era_authority_contract.md`; it is named here because a session reading this
section for "what breaks compatibility" would otherwise read a bump as evidence
of a layout change.

**The detectors are what make a fallback clean, and this decision does not
reach them.** `ERA_EEPROM_RESET_KEY`, the reset-guard record, the board files'
strict reset, and every signature/magic/prefix validity check stay exactly as
they are. Retiring compatibility while weakening the thing that catches a stale
block is precisely how outcome B is produced.

## Closed Boundaries

Which wire, route, and runtime surfaces are closed is canonical in
`era_closed_surface_contract.md` and `era_invariants.md`. One boundary is this
document's own, because it constrains source shape rather than a surface:

- No compatibility boundary around the former master-scheduler and sync-lane
  layers. The replacement lane deleted them outright rather than wrapping
  them; reintroducing a wrapper restores the ownership ambiguity the deletion
  removed.

What earns a structural change is in **Source Editing Rules** above.
