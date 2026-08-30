# ERA Walkthrough

Genre: map
Canonical for: the end-to-end paths — one keyboard pass, a key on each kind of
half, a configuration change reaching both halves, boot up to the wire opening,
and suspend through wake. It maps each path onto the files it passes through

**It is a map, not a contract.** Every rule it touches is canonical somewhere
else and linked. If this page and a contract disagree, the contract is right and
this page is the bug.

## 1. One pass of the keyboard loop

QMK's `main()` runs `keyboard_task()` (`quantum/keyboard.c`) and then the
protocol and housekeeping tails, forever; on a no-cable TOMAK79H half that
whole loop is about **17.8 µs** (`era_performance_gates.md`, Fixed Baselines).

The pass-phase instrument tiles it into twelve contiguous segments
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

The matrix is sampled by hardware, so segment 1 costs core0 one frame copy
(3.18 µs) rather than a bit-banged scan. Nothing in the loop polls the peer:
the wire runs on core1 under a standing grant, and core0 enters segment 11 only
when an event or a deadline says so (`era_route_contract.md`, Due/Deadline Model).

## 2. A key on the half that owns the USB session

The ordinary QMK path, deliberately untouched.

1. **Segments 1–2.** `matrix_scan()` (`system/era_rp2040_matrix_core.c`) fetches the newest complete PIO frame into `raw_rows[]`, debounces, and folds into `composed_rows[]`.
2. **Segments 5–6.** `matrix_task()` (`quantum/keyboard.c`) differences `matrix_get_row()` against `matrix_previous[]` and calls `action_exec()` for each changed position.
3. **Action.** `quantum/action_tapping.c` resolves the keycode against the layer state and tap-hold (ERA seam in `era_qmk_fork_ledger.md`) and registers into the report.
4. **HID out** on this half's own USB.

QMK's matrix, debounce and key processing stay the sole HID producer. No
transport path may inject a key event (`era_invariants.md`).

## 3. A key on the half that does **not** own the session

In HOST-PEER the enumerated **HOST** and the **PEER** share one report. A PEER
key arrives as *matrix state*, never as HID.

### On the PEER (the wire initiator)

1. **Segments 1–2.** Same as section 2: the PEER debounces its own rows in `system/era_rp2040_matrix_core.c`.
2. **Segment 3.** `era_split_transport_scheduler_transport_step()` (`split/era_split_transport_scheduler.c`) publishes an immutable local snapshot through `era_matrix_engine_publish_local_snapshot_if_needed()`.
3. The route-due recompute (`scheduler/era_split_transport_scheduler_timing.c`) asks `era_matrix_engine_source_push_due()` (`system/era_rp2040_matrix_core.c`) and raises the source-push route bit. **Matrix dirty carries no cadence** — due immediately, without opening full planning (`era_route_contract.md`).
4. `era_host_peer_matrix_link_capture_source_push()` (`split/era_host_peer_matrix_link.c`) copies the rows, packs them (`split/era_split_matrix_frame.c`), and enqueues them on core1's `LANE_SOURCE_PUSH` (`communication_core/era_split_communication_core_initiator.h`).
5. **Core1 takes it; core0 is done.** `era_split_communication_core_process_initiator()` (`communication_core/era_split_communication_core_host_peer_lanes.c`) frames it (`split/era_split_wire_frame.c`) and sends it through the RP2040 PIO backend (`split/era_split_transaction_backend_rp2040.c`), then parks on the one wait primitive until the response window opens or its deadline expires.

### On the HOST (the wire responder)

6. `communication_core/era_split_communication_core_responder_service.c` receives the frame, reserves result capacity before answering, ACKs in the admitted response slot, and publishes the result for core0.
7. `scheduler/era_split_transport_scheduler_responder.c` drains it into `era_host_peer_matrix_link_accept_source_push_packed()` (`split/era_host_peer_matrix_link.c`), which unpacks and calls `era_matrix_engine_accept_peer_snapshot()` (`system/era_rp2040_matrix_core.c`). **This writes a cache and nothing else.**
8. On the HOST's next scan, segment 3 calls `era_matrix_engine_sync_peer_projection()` (`system/era_rp2040_matrix_core.c`), which folds the accepted peer rows into `composed_rows[]` beside the HOST's own.
9. From there it is section 2: `matrix_task()` (`quantum/keyboard.c`) differences the composed rows and `action_exec()` produces the report. **The PEER's key becomes a HID event by being matrix state on the HOST when the HOST scans.**

### Back on the PEER

10. The ACK returns through the same core1 lane; `scheduler/era_split_transport_scheduler_routes.c` drains it and tells the engine the sequence was accepted, which retires the pending push.

**In DUAL-HOST none of this runs.** Both halves are enumerated, each delivers
its own report, and the matrix, digest and mirror routes are closed by
invariant, so the wire carries render and resolution state only
(`era_invariants.md`).

`era_host_peer_matrix_contract.md` owns the model, `era_wire_contract.md` the
frame, `era_route_contract.md` when it may run, `era_authority_contract.md`
which half is which.

## 4. A configuration change reaching both halves

A VIA edit on the HOST must end up in the PEER's EEPROM: a PEER that loses the
relation runs standalone on its last durable image.

1. **VIA command in.** `quantum/via.c` dispatches to `via_custom_value_command_kb()` (`split/era_split_board.c`), then the system, keyboard and feature channels (`system/era_common_via.c`, `split/era_split_via_sync.c`).
2. **Apply, then schedule persist.** Three gates, all `ERA_STORAGE_QUIET_DEFER_MS`, armed by the save and never by the set: `quantum/rgb_matrix/rgb_matrix.c` (RGB Matrix eeconfig via `quantum/eeconfig.h`), `quantum/rgblight/rgblight.c` (underglow), `system/era_board_hooks.c` (ERA backlight, ERA RGB indicator, board config). Nothing else on the keyboard channel is deferred — tap dance, SOCD, debounce, tapping, mousekey and the sync policy write at their save.
3. **Capture at the cold boundary.** `era_host_peer_storage_task()` (`split/era_host_peer_storage.c`) takes a settled capture of the seven portable domains, CRCs each, and steps the **storage news value** if this capture departs from the last agreement.
4. **The news value crosses on the relation's own lane.** The responder advertises a forward-only counter; the initiator noticing it move runs a whole-family `SYNC_STATUS` summary. The value names no domain (`era_host_peer_storage_contract.md`).
5. **Arbitration** is latest-change-wins over the persisted recency layer, and never who initiates. Every request is PEER-initiated in HOST-PEER and Left-initiated in DUAL-HOST, by invariant.
6. **Transfer, then durable apply.** Chunks under wire exclusivity; exclusivity ends at transfer-verified; the receiver revalidates authority/preconditions and crosses ADMIT. One synchronous `era_nvm_replace()` (`storage/era_nvm.c`, `storage/era_eeprom_driver.c`) commits the complete domain; QMK readers see the old image until that commit succeeds and the new image immediately after. Core1 keeps relation liveness running from SRAM while core0 is inside the NVM call.
7. **What this costs the typist** is `era_host_peer_storage_contract.md`, **What The Lane Costs A Typist**.

## 5. Boot, from reset to the wire opening

The order is load-bearing (`era_invariants.md`, `era_sram_residency_contract.md`).

1. **boot2** fetches the reset PC from the `.vectors` LMA in flash — the one vector slot that must stay a flash address.
2. **crt0 pre-copy** (`lib/chibios/os/common/startup/ARMCMx/compilers/GCC/crt0_v6m.S`) masks interrupts, then `__early_init` in `system/era_boot_core1_halt.c` hardware-halts core1 through PSM `FRCE_OFF` and arms the double-tap bootloader window after classifying the reset. Nothing in this window may touch SRAM or spin unbounded.
3. **The copy loops** move `.sram_image` into RAM, clear `.bss`, and zero the no-init regions — the window core1 had to be halted for, because it overwrites core1's code, data, stack and vector table.
4. **`__late_init`** performs the bootloader jump if the magic says REQUEST. Arming and jumping are split across the copy loops and must stay split.
5. **`main()`** runs `protocol_pre_init()` — USB D+ pull-up asserted unconditionally, so every reset attaches — then `keyboard_init()`, whose `timer_init()` rebases the clock. **A timestamp taken before this point is not comparable after it.**
6. **`keyboard_pre_init_kb`** → USB identity and authority-reducer init (`split/era_split_keyboard.c`).
7. **`matrix_init_kb`** → the ERA engine, the PIO sampler, the feature modules. `split_util.c` calls `transport_slave_init()`, which reaches `era_split_transport_scheduler_init()` (`split/era_split_transport_scheduler.c`) — **policy only. It plans the relation and the wire role and opens nothing.**
8. **`keyboard_post_init_kb`** → `era_split_keyboard_post_init()` (`split/era_split_keyboard.c`) runs storage init and then the **single named step that opens the wire**, `era_split_transport_scheduler_start_communication_core()`. It returns whether the wire came up and the call site discards that deliberately: the failure handling lives inside the step.
9. **Core1** runs its standing exchange from there on.

`is_keyboard_master()` is false for the whole of `keyboard_init()`
(`quantum/keyboard.c`; the ERA projection is
`split/era_split_authority_reducer.c`'s), so only the slave hook runs at boot on
both halves (`era_authority_contract.md`).

## 6. Suspend, the hold, and wake

1. **The host suspends.** `system/era_usb_session.c` owns every fact about the local session: the configure-state remap, the single SOF frame-counter sampler, and the has-a-host-ever latch that keeps a charger from reading as a sleeping host.
2. **The authority reducer** folds those into a local authority record; the mode planner turns the pair of authority records into a relation.
3. **Both halves reporting no host is the absence of the fact that assigns roles, not a fact that unassigns them**, so the relation is *held* rather than torn down (`era_authority_contract.md`, Relation Hold). Core1 keeps the standing exchange running for the whole no-host span.
4. **Lighting sleeps** through a separate decision with two detectors — the suspend state and the SOF frame-loss arm — because a dead host behind a live cable stops sending frames without ever announcing a suspend.
5. **A key wakes it.** On the HOST half the composed-input edge fires the remote wake directly. **On a PEER half it works only because the exchange is still running** — the composed matrix input the wake fires on is produced by the relation, which is the reason the hold exists at all.
