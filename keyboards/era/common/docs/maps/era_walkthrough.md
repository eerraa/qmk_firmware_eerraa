# ERA Walkthrough

Status: active
Genre: map
Canonical for: the end-to-end paths — one keyboard pass, a key on each kind of
half, a configuration change reaching both halves, boot up to the wire opening,
and suspend through wake. It maps each path onto the files it passes through
Read when: you need to know **where** a change goes, before you read the
contract that says **what** it must preserve

The contracts each describe one surface and assume the others. This document is
the other axis: it follows five things all the way through and names the file at
every step, so a contract read afterwards decodes on first contact.

**It is a map, not a contract.** Every rule it touches is canonical somewhere
else and linked. If this page and a contract disagree, the contract is right and
this page is the bug.

## 1. One pass of the keyboard loop

Everything below happens inside this. QMK's `main()` runs `keyboard_task()`
(`quantum/keyboard.c`) and then the protocol and housekeeping tails, forever;
on a no-cable TOMAK79H half that whole loop is about **17.8 µs**
(`era_performance_gates.md`, Fixed Baselines).

The pass-phase instrument tiles it into twelve contiguous segments, and the
twelve are also the best index into the code
(`system/era_pass_phase_diagnostics.h` for the ids,
`era_capture_reading.md` for how to read them):

| # | segment | what runs | where |
| ---: | --- | --- | --- |
| 1 | `RAW` | one PIO+DMA frame fetched and decoded into `raw_rows[]` | `system/era_rp2040_matrix_pio.c` |
| 2 | `DEB` | debounce, composed rows, local-change publish | `system/era_rp2040_matrix_core.c`, `system/era_matrix_debounce_runtime.c` |
| 3 | `XPORT` | the split transport step | `split/era_split_transport_scheduler.c` |
| 4 | `SCANHK` | `matrix_scan_kb()`, the composed-input edge | `system/era_rp2040_matrix_core.c` |
| 5 | `DIFF` | difference against `matrix_previous[]` | `quantum/keyboard.c` |
| 6 | `ACT` | the changed-row walk into `action_exec()` | `quantum/keyboard.c`, `quantum/action.c` |
| 7 | `QTM` | `quantum_task()` | `quantum/quantum.c` |
| 8 | `RGB` | `rgb_matrix_task()` | `quantum/rgb_matrix/rgb_matrix.c` |
| 9 | `KTAIL` | `mousekey_task()`, `led_task()` | `quantum/` |
| 10 | `LOOP` | `protocol_post_task()`, raw-HID, console, deferred exec | `quantum/`, `tmk_core/` |
| 11 | `HK` | `era_split_keyboard_task()` — features, scheduler, the 1 ms tick | `split/era_split_keyboard.c` |
| 12 | `REST` | back to the top of the next `matrix_scan()` | — |

**Two things about this loop decide most of what the rest of the architecture
looks like.** The matrix is sampled by hardware, so segment 1 costs core0 one
frame copy (3.18 µs) rather than a bit-banged scan. And **nothing in the loop
polls the peer**: the wire runs on core1 under a standing grant, and core0
enters segment 11 only when an event or a deadline says so
(`era_route_contract.md`, Due/Deadline Model).

## 2. A key on the half that owns the USB session

This is the ordinary QMK path and it is deliberately untouched.

1. **Segments 1–2.** `matrix_scan()` fetches the newest complete PIO frame,
   decodes it into `raw_rows[]`, debounces into the local rows, and folds the
   local rows into `composed_rows[]`
   (`system/era_rp2040_matrix_core.c`).
2. **Segment 5–6.** `matrix_task()` (`quantum/keyboard.c`) reads
   `matrix_get_row()`, differences it against `matrix_previous[]`, and calls
   `action_exec()` for each changed position.
3. **Action processing** resolves the keycode against the layer state, runs
   tap-hold if the key has it (`quantum/action_tapping.c`, with the ERA seam
   described in `era_qmk_fork_ledger.md`), and registers into the report.
4. **HID out** on this half's own USB.

**The invariant that binds everything else is here**: QMK's matrix, debounce and
key processing stay the sole HID producer. No transport path may inject a key
event (`era_invariants.md`).

## 3. A key on the half that does **not** own the session

In HOST-PEER, one half is enumerated (the **HOST**) and one is not (the
**PEER**). A PEER's keys must arrive in the HOST's report — and they get there
as *matrix state*, never as HID.

This is the path with the most moving parts and the one worth reading twice.

### On the PEER (the wire initiator)

1. **Segments 1–2** exactly as above: the PEER debounces its own rows.
2. **Segment 3**, `era_split_transport_scheduler_transport_step()`
   (`split/era_split_transport_scheduler.c`). It publishes an immutable local
   snapshot through `era_matrix_engine_publish_local_snapshot_if_needed()`.
3. The route-due recompute asks `era_matrix_engine_source_push_due()`
   (`scheduler/era_split_transport_scheduler_timing.c`) and raises the
   source-push route bit. **Matrix dirty carries no cadence** — it is due
   immediately, without opening full planning (`era_route_contract.md`).
4. The same pass's initiator step builds the request payload:
   `era_host_peer_matrix_link_capture_source_push()`
   (`split/era_host_peer_matrix_link.c`) copies the rows out of the engine and
   packs them into the half-matrix bit format
   (`split/era_split_matrix_frame.c`). It is enqueued on core1's
   `LANE_SOURCE_PUSH`
   (`communication_core/era_split_communication_core_initiator.h`).
5. **Core1 takes it from here and core0 is done.**
   `era_split_communication_core_process_initiator()`
   (`communication_core/era_split_communication_core_host_peer_lanes.c`) hands
   it to the transaction engine, which frames it
   (`split/era_split_wire_frame.c`) and sends it through the RP2040 PIO backend
   (`split/era_split_transaction_backend_rp2040.c`). Core1 then **parks** on the
   one wait primitive until the response window opens or its deadline expires.

### On the HOST (the wire responder)

6. Core1's responder service
   (`communication_core/era_split_communication_core_responder_service.c`)
   receives the frame, **reserves result capacity before answering**, ACKs in
   the admitted response slot, and publishes the result for core0.
7. Core0 drains it at the next housekeeping pass
   (`scheduler/era_split_transport_scheduler_responder.c`) into
   `era_host_peer_matrix_link_accept_source_push_packed()`, which unpacks and
   calls `era_matrix_engine_accept_peer_snapshot()`. **This writes a cache and
   nothing else.**
8. On the HOST's **next scan**, segment 3 calls
   `era_matrix_engine_sync_peer_projection()`, which folds the accepted peer
   rows into `composed_rows[]` beside the HOST's own.
9. From there it is section 2 verbatim: `matrix_task()` differences the composed
   rows and `action_exec()` produces the report. **The PEER's key becomes a HID
   event by being matrix state on the HOST when the HOST scans.**

### Back on the PEER

10. The ACK returns through the same core1 lane; core0 drains it
    (`scheduler/era_split_transport_scheduler_routes.c`) and tells the engine
    the sequence was accepted, which is what retires the pending push.

**In DUAL-HOST none of this runs.** Both halves are enumerated, each delivers
its own report, and the matrix, digest and mirror routes are closed by
invariant — which is why DUAL-HOST is the low-latency configuration and why
its wire carries render and resolution state only (`era_invariants.md`).

Contracts for this path: `era_host_peer_matrix_contract.md` (the model),
`era_wire_contract.md` (the frame), `era_route_contract.md` (when it may run),
`era_authority_contract.md` (which half is which).

## 4. A configuration change reaching both halves

A VIA edit on the HOST has to end up in the PEER's EEPROM, because a PEER that
loses the relation runs standalone on its last durable image.

1. **VIA command in.** `quantum/via.c` dispatches to
   `via_custom_value_command_kb()`, which the split class skeleton owns
   (`split/era_split_board.c`); it routes the system channel, then the keyboard
   channel, then the feature channel (`system/era_common_via.c`,
   `split/era_split_via_sync.c`).
2. **The feature module applies the value** and the save that follows it
   *schedules* a persist rather than performing one. **Which mechanism holds
   it depends on which save event the client sent**, and there are three,
   one per burst-prone persistence range: `quantum/eeconfig.h`'s deferred-write
   layer for the RGB Matrix eeconfig block (instantiated in
   `quantum/rgb_matrix/rgb_matrix.c`), a gate of the same shape in
   `quantum/rgblight/rgblight.c` for the underglow block, and the keyboard
   channel's gate in `system/era_board_hooks.c` for the ERA backlight, the ERA
   RGB indicator and the board's own config record. All three take the same
   number, `ERA_STORAGE_QUIET_DEFER_MS`, and all three are armed by the save
   and never by the set, so dragging a VIA slider costs one flash write and
   not fifty. Nothing else on the keyboard channel is deferred -- tap dance,
   SOCD, debounce, tapping, mousekey and the sync policy write at their save,
   which is correct because none of them has a continuous control.
3. **The storage engine captures at the cold boundary.**
   `era_host_peer_storage_task()` (`split/era_host_peer_storage.c`) runs from
   housekeeping, takes a settled capture of the seven portable domains, CRCs
   each, and steps the **storage news value** if this capture departs from the
   last agreement.
4. **The news value crosses on the relation's own lane** — the responder
   advertises a forward-only counter, and the initiator noticing it move is what
   makes it run a whole-family `SYNC_STATUS` summary. The value names no domain;
   that is the whole design (`era_host_peer_storage_contract.md`).
5. **Arbitration decides whose content wins** — latest-change-wins over the
   persisted recency layer — and never who initiates. Every request is
   PEER-initiated in HOST-PEER and Left-initiated in DUAL-HOST, by invariant.
6. **Transfer, then durable apply.** The image crosses as chunks under wire
   exclusivity; exclusivity ends at transfer-verified, and the receiving half
   then writes it to EEPROM in slices, yielding the keyboard pass between
   sectors (`system/era_flash_slice.c`, `quantum/wear_leveling/wear_leveling.c`).
7. **What this costs the typist** is written down and is not a defect:
   `era_host_peer_storage_contract.md`, **What The Lane Costs A Typist**.

## 5. Boot, from reset to the wire opening

The order here is load-bearing and most of it is invariant
(`era_invariants.md`, `era_sram_residency_contract.md`).

1. **boot2** fetches the reset PC from the `.vectors` LMA in flash — the one
   vector slot that must stay a flash address.
2. **crt0 pre-copy**
   (`lib/chibios/os/common/startup/ARMCMx/compilers/GCC/crt0_v6m.S`) masks
   interrupts, then calls
   `__early_init`, which ERA overrides in `system/era_boot_core1_halt.c`. Two
   unrelated jobs live there because the hardware offers one call on this side
   of the copy loops: **core1 is hardware-halted** through PSM `FRCE_OFF`, and
   the **double-tap bootloader window is armed** after classifying the reset.
   Nothing in this window may touch SRAM or spin unbounded.
3. **The copy loops** move `.sram_image` into RAM, clear `.bss`, and zero the
   no-init regions. This is the window core1 had to be halted for: it overwrites
   core1's code, data, stack and vector table.
4. **`__late_init`** performs the bootloader jump if the magic says REQUEST.
   Arming and jumping are split across the copy loops and must stay split.
5. **`main()`** runs `protocol_pre_init()` — which asserts the USB D+ pull-up
   unconditionally, so every reset attaches — and then `keyboard_init()`, whose
   `timer_init()` rebases the clock. **A timestamp taken before this point is
   not comparable after it.**
6. **`keyboard_pre_init_kb`** → USB identity and authority-reducer init
   (`split/era_split_keyboard.c`).
7. **`matrix_init_kb`** → the ERA engine, the PIO sampler, the feature modules.
   `split_util.c` calls `transport_slave_init()`, which reaches
   `era_split_transport_scheduler_init()` — **policy only. It plans the relation
   and the wire role and opens nothing.**
8. **`keyboard_post_init_kb`** → `era_split_keyboard_post_init()` runs storage
   init and then the **single named step that opens the wire**,
   `era_split_transport_scheduler_start_communication_core()`
   (`split/era_split_keyboard.c`). It returns whether the wire came up and the
   call site discards that deliberately: the failure handling lives inside the
   step, and a give-up state at the caller would need a rule for when to resume
   that no observed failure has asked for.
9. **Core1 runs** its standing exchange from there on.

`is_keyboard_master()` is false for the whole of `keyboard_init()`
(`quantum/keyboard.c`; the ERA projection is
`split/era_split_authority_reducer.c`'s), so only the slave hook runs at boot on
both halves — a fact that has caught more than one change
(`era_authority_contract.md`).

## 6. Suspend, the hold, and wake

1. **The host suspends.** The USB session unit
   (`system/era_usb_session.c`) owns every fact about the local session: the
   configure-state remap, the single SOF frame-counter sampler, and the
   has-a-host-ever latch that keeps a charger from reading as a sleeping host.
2. **The authority reducer** folds those into a local authority record, and the
   mode planner turns the pair of authority records into a relation.
3. **Both halves reporting no host is the absence of the fact that assigns
   roles, not a fact that unassigns them**, so the relation is *held* rather
   than torn down (`era_authority_contract.md`, Relation Hold). Core1 keeps the
   standing exchange running for the whole no-host span.
4. **Lighting sleeps** through a separate decision with two detectors — the
   suspend state and the SOF frame-loss arm — because a dead host behind a live
   cable stops sending frames without ever announcing a suspend.
5. **A key wakes it.** On the HOST half the composed-input edge fires the remote
   wake directly. **On a PEER half it works only because the exchange is still
   running** — the composed matrix input the wake fires on is produced by the
   relation, which is the reason the hold exists at all.

## Where to go next

| you are changing | read first |
| --- | --- |
| anything on the wire | `era_wire_contract.md`, then `era_route_contract.md` |
| which half may do what | `era_authority_contract.md` |
| the matrix engine or the sampler | `era_sram_residency_contract.md`, `era_source_map.md` |
| storage | `era_host_peer_storage_contract.md` |
| a build option or a new board | `era_build_options.md`, `era_board_adoption.md` |
| a QMK core file | `era_qmk_fork_ledger.md` |
| anything at all, before you believe it works | `era_performance_gates.md` |
