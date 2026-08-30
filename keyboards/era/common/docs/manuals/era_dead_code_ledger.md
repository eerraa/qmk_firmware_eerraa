# ERA Dead Code Ledger

Genre: manual
Canonical for: measured unused/retired ERA surfaces in this tree (not VIA/H7S)

This file is an inventory. Later sessions delete one claim-cluster at a time.
It does not delete firmware, tests, JSON, or other documents.

## Classes

Every row uses exactly one class:

- **DELETE** — zero compile, link, or callers in every ERA image this tree
  builds (rules.mk `SRC`, `$(wildcard)`, board `post_rules.mk`, tests). Safe
  to remove in a later session.
- **RETIRED-ID** — a numeric id, mode, or due-bit kept unused so it is not
  reused (recycle rule in `maps/era_identifier_map.md`). Do not delete the
  named constant if removing it would shift later ids.
- **STALE-COMMENT** — comment or document prose that is false; the code is
  live.
- **PAIRED-STOP** — this QMK tree looks unused at a name, but VIA
  (`eerraa/the-via-eerraa`) or H7S (`eerraa/eerraa-qmk-h7s-fw`) still names
  it. Both sides are recorded. This ledger does not pick a winner.
- **KEEP** — false positive: used here, or a QMK-user API this fork still
  ships even if ERA images do not link the implementation.

## Measurement bounds

Recomputed from this tree at `work/era-nvm`
`dc2ebf485b748bf8c74fe5eee782b8be70606784` (identifier_map PR 45 merged).
`tools/era_doc_refs.py` is locatability, not truth.

Peer trees were read, not edited:

- VIA `eerraa/the-via-eerraa` `a37dfaa768792d8a480621a120b80affb0fd13cd`
- H7S `eerraa/eerraa-qmk-h7s-fw` `101c021edbd104106c271e01b489989bc1f20589`

Proof in this file is source, make rules, and those peer trees. This sitting
did not link an ELF, did not flash, and invents no device evidence.

`sirind/brick65` is atmega32u4 and takes none of the ERA common layer
(`manuals/era_board_adoption.md` Copy-To-RAM Policy). Split and copy-to-RAM
claims below are the twenty-two RP2040 boards.

## REFUSED

> **REFUSED:** classify modes 5 and 6 as DELETE, or as free to reuse
> **WHY:** `split/era_split_mode_planner.h` says nothing holds them now, so 5
> is the next relation's; `maps/era_identifier_map.md` (merged PR 45) says they
> stay un-reused; closed PR 48 agreed with the header
> **REOPENS:** an owner picks one of those two sentences

> **REFUSED:** delete `ERA_SPLIT_EEPROM_SYNC_STATUS_RESULT_FULL`
> **WHY:** it is a numbered wire status in `split/era_split_eeprom_sync.h`;
> deleting it renumbers TIMEOUT, ROLE_CHANGED, and SOURCE_CHANGED
> **REOPENS:** a new status appended after SOURCE_CHANGED, not a hole in the
> middle

> **REFUSED:** delete debounce STATUS (channel 14 value 5) from ERA firmware
> **WHY:** H7S still names `id_qmk_debounce_status`; this tree's VIA JSON and
> the VIA peer do not
> **REOPENS:** both firmwares and both definition trees drop the id, or an
> owner names one winner

> **REFUSED:** pick a winner between the VIA exact-ms and legacy term bands,
> or delete either band or the `keymaps/via` JSON
> **WHY:** both bands stay; firmware in `features/era_tapdance_via.c` and
> `features/era_tapping_via.c` answers both; tree JSON in each board's
> `keymaps/via` folder is the stock-VIA/legacy surface; the VIA app
> (`the-via-eerraa`) presents exact-ms; this is intentional dual compatibility
> with usevia.app, not an unresolved mismatch
> **REOPENS:** an explicit owner request to drop one surface

> **REFUSED:** restore Graphify, or close H7S `state_open`
> **WHY:** out of scope for this ledger; the tracked graphify-out directory is
> already gone; `state_open` is the other firmware family
> **REOPENS:** an explicit owner request on that other repository

> **REFUSED:** delete QMK `transport_master` / `transport_slave` /
> `transport_execute_transaction` or the ChibiOS serial implementations from
> this fork as ERA dead code
> **WHY:** they are QMK-user API and other keyboards' link surface;
> `manuals/era_qmk_fork_ledger.md` already records that ERA split does not
> link the serial files
> **REOPENS:** an upstream rebase that drops the symbols, or an owner
> decision that this fork no longer carries them

## DELETE

No pending DELETE rows.

### Removed

| ID | Removed | Proof |
| --- | --- | --- |
| DC-empty-ifdef | 2026-08-30, PR 50 | Re-measured before delete: the first `#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE` … `#endif` in `split/communication_core/era_split_communication_core_diagnostics.c` still wrapped only a comment (zero non-comment tokens). `era_split_communication_core_get_diagnostics_snapshot()` in that `.c` remains on the ungated `SRC` line in `split/era_split_qmk_rules.mk`. The later `#ifdef ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE` in the same function still fills 33 responder snapshot assignments. The comment-only pair is gone; that live pair is not. |

No other comment-only `#ifdef` / `#endif` pair exists under `keyboards/era`.
No `#if 0` exists under `keyboards/era`.

## RETIRED-ID

Do not delete these in a later session if deletion would shift a later live
id. Recycle rule: `maps/era_identifier_map.md`.

| ID | Surface | Proof |
| --- | --- | --- |
| RI-due-bits | Scheduler route-due bits 1, 3, 5, and 6 | `split/era_split_scheduler_events.h` enumerates live `ERA_SPLIT_SCHEDULER_ROUTE_DUE_ATTACH_STATUS`, `ERA_SPLIT_SCHEDULER_ROUTE_DUE_HOST_PEER_SOURCE_PUSH`, and `ERA_SPLIT_SCHEDULER_ROUTE_DUE_DUAL_RUNTIME_PUSH` only. The header comment names the four retired bits and the capture `due=` reason they stay unassigned. File-local dirty bits in `split/era_split_transport_scheduler.c` are a different word and are live. |
| RI-result-full | `ERA_SPLIT_EEPROM_SYNC_STATUS_RESULT_FULL` in `split/era_split_eeprom_sync.h` | The enumerator is the only hit in this tree. TIMEOUT / ROLE_CHANGED / SOURCE_CHANGED follow it. Distinct from live `ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESULT_FULL`. H7S at the SHA above does not name `RESULT_FULL`. |
| RI-payload-4-6 | Wire payload kind values four and six | `split/era_split_wire_protocol.h` assigns INVALID, GRANT_ACK, SESSION_STATUS, EEPROM_SYNC, then HOST_PEER, then HOST_PEER_HOST_SOURCE_RSP. Comments in that header: four was ERROR_NACK (class `0x40`), six was DUAL_HOST. Classifier default-rejects unmatched class. `maps/era_identifier_map.md` already records both as stay-allocated. |
| RI-session-0x20 | SESSION_STATUS flag `0x20` | `split/era_split_wire_protocol.h` lists live flags HOST_OPEN, NO_HOST, RESPONSE_REQUESTED, MATRIX_READY, BULK_PAGE. The header comment: `0x20` was `dual_host_ready` and stays retired un-reused. Encoder mask omits it; the decoder refuses bits outside `ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK`. |
| RI-owner-1 | Communication-core backend owner value 1 | `split/communication_core/era_split_communication_core_owner.h` assigns `ERA_SPLIT_COMMUNICATION_CORE_BACKEND_OWNER_NONE` then `_CORE1`. No enumerator occupies 1. |
| RI-snap-src-1 | Responder snapshot source value 1 | File-local enum in `split/communication_core/era_split_communication_core_responder.c`: NONE then CORE0. No enumerator occupies 1. |
| RI-queue-kind-1 | Queue record kind value 1 | `split/communication_core/era_split_communication_core_internal.h` assigns INVALID, then INITIATOR_REQUEST, then INITIATOR_RESULT. No enumerator occupies 1. |
| RI-via-sync-old | Retired SYSTEM-channel parent / `*_EFFECTIVE` / per-relation EEPROM value ids | Constants are gone from `split/era_split_via_sync.h`. Live ids there are EEPROM requested, INPUT requested, RGB requested. `maps/era_identifier_map.md` **VIA Sync Value IDs**: the retired numbers stay un-reused. |

SESSION_STATUS bits `0x04` and `0x08` are unassigned. The decoder in
`split/era_split_wire_payload.c` refuses them because they sit outside
`ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK` in `split/era_split_wire_protocol.h`.
That header says a future flag may take a mask line. They are not the same
class as `0x20`. Not DELETE.

### Modes 5 and 6 (not classified DELETE vs free)

`split/era_split_mode_planner.h` names five enumerators: LOCAL_NO_LINK, the two
HOST-PEER roles, the two DUAL-HOST roles. The comment in that header: a
HOST_HOST pair once occupied 5 and 6; nothing holds them now, so 5 is the next
relation's if there ever is one.

`maps/era_identifier_map.md` **Modes** (this tree, PR 45): values 5 and 6 have
no enumerator (were HOST_HOST_LEFT / HOST_HOST_RIGHT) and stay un-reused.

Closed PR 48's body agreed with the header and was not merged.

This ledger records both sentences. It does not pick. See REFUSED.

## STALE-COMMENT

Code or make rules are live. The prose was false.

No pending STALE-COMMENT rows.

### Corrected

| ID | Corrected | Proof |
| --- | --- | --- |
| SC-fifteen | 2026-08-30, PR 51 | Re-measured: 23 `keyboard.json` files; 22 RP2040. 19 non-split `post_rules.mk` include `system/era_sram_resident_rules.mk`; three split boards take it via `split/era_split_qmk_rules.mk`. `sirind/brick65` is atmega32u4 and does not. Linker comment in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` now says twenty-two RP2040 boards. Lighting "fifteen boards" in `keyboards/era/era_build_options.mk` is a different count (boards that assign lighting selectors) and was not this sentence. |
| SC-vector-35 | 2026-08-30, PR 51 | Re-measured: `system/era_vector_defaults.c` defines 38 `ERA_VECTOR_DEFAULT` aliases (33 unconditional + five conditional PWM/PIO/DMA). Per-image count still varies with PWM / SPLIT / `RP_DMA_REQUIRED`. The file no longer claims `nm` listed "exactly these 35 names" for the enumeration. The residency gate in `tools/era_residency_gate.sh` still reads table bytes, not this list. |
| SC-hb-enable | 2026-08-30, PR 51 | Re-measured: `split/era_split_qmk_rules.mk` lists `ERA_SPLIT_COMMUNICATION_CORE_HEARTBEAT_ENABLE` in `ERA_SPLIT_COMMUNICATION_CORE_LEGACY_STAGE_VARS` and `$(error)`s if any is set. Zero `-D` of that name under `keyboards/era`, `quantum/`, `platforms/`, `drivers/`, `tmk_core/`. Stage must be `CORE1_FULL`. Comment in `split/communication_core/era_split_communication_core_lifecycle_rp2040.c` now states that. |
| SC-hb-lane | 2026-08-30, PR 51 | Re-measured: `split/communication_core/era_split_communication_core_initiator.h` enumerates INVALID, SESSION_STATUS, SOURCE_PUSH only. Per-lane ok/miss/bad/fail remain for those live lanes in `split/diagnostics/era_split_wire_diagnostics.c`. Wire-diag `hb=` is `ERA_SPLIT_TRANSACTION_TIMING_BUCKET_HEARTBEAT_ACK` from `split/era_split_transaction_engine.c` via `split/diagnostics/era_split_wire_diagnostics.c`. Standing path in `split/communication_core/era_split_communication_core_standing.c` stamps `ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT`; responder kind HEARTBEAT is live in `split/communication_core/era_split_communication_core_responder.h`. Present-tense "lane id kept" / `hb=` 0/0 prose in `split/scheduler/era_split_transport_scheduler_routes.c` and `split/era_split_wire_protocol.h` is corrected. KEEP-hb unchanged. |
| SC-graph | 2026-08-30, PR 51 | Re-measured: `git ls-files` has no graphify-out directory and no graphifyignore file. `era_active_index.md` **What This Repository Does Not Carry** no longer lists the graph among what the orphan ships. `AGENTS.md` already omitted that payload (Korean pointer kept). Ledger mentions remain measurement notes. |

Historical stack figures (Slice 11 952 B, old 1024 B reservation) in
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c` are
labeled as past walks. The same comment then sets the reservation to 2 KiB.
Default `ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS` in that file is 512 words.
`manuals/era_performance_gates.md` walker used-depth (912 / 992 / 1000 B) is
judged against a 2048 B reservation. That is not a present-tense false
reservation claim. Not STALE-COMMENT.

## PAIRED-STOP

Both sides recorded. No winner.

### PS-debounce-status

- **This firmware:** `features/era_debounce_via.c` enumerates STATUS after
  MODE, TIME_SINGLE, TIME_PRE, TIME_POST. SET STATUS returns true and writes
  nothing. GET STATUS always returns `ERA_DEBOUNCE_STATUS_READY`.
- **This tree JSON:** VIA definitions under `keyboards/era/**/keymaps/via/`
  present MODE, TIME_SINGLE, TIME_PRE, TIME_POST. No `id_qmk_debounce_status`.
- **VIA peer at the SHA above:** no `id_qmk_debounce_status`.
- **H7S at the SHA above:** that tree's via.h assigns
  `id_qmk_debounce_status`. That tree's debounce_profile.c GET/SET STATUS
  (GET uses a real status byte). Those files are not in this repository.

Do not delete the ERA STATUS arm until that pair is resolved. See REFUSED.

TIME_SINGLE GET and TIME_PRE GET both return
`era_matrix_debounce_config_get_press_delay()` from
`system/era_matrix_debounce_config.c`. SET TIME_SINGLE still writes both
delays via `era_matrix_debounce_config_set_single_delay()` in that file. This
tree's JSON presents both ids. That mapping is live. Not dead. KEEP-debounce-get
below.

## KEEP

Hunches that measured live, shipped, or out-of-scope.

| ID | Hunch | Proof |
| --- | --- | --- |
| KEEP-hb | Core0 HEARTBEAT initiator lane retired, therefore all HEARTBEAT is dead | Initiator lane enumerator is gone (`era_split_communication_core_initiator.h`). Standing path `era_split_communication_core_standing.c` stamps `ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT` from `split/era_split_wire_router.h`. Responder result kind HEARTBEAT is live in `split/communication_core/era_split_communication_core_responder.h` and `split/communication_core/era_split_communication_core_responder_service.c`. Coalescing: `era_split_communication_core_responder_result_policy.h`. Route kind is kept so capture `rk` stays stable (`era_split_wire_router.h`). |
| KEEP-transport | `transport_master` / `transport_slave` / `transport_execute_transaction` unused, so delete them | `split/era_split_transport.c` exports only `transport_master_init` / `transport_slave_init`, both `era_split_transport_scheduler_init()` in `split/era_split_transport_scheduler.c`. `CUSTOM_MATRIX = yes` in `system/era_common_qmk_rules.mk` skips `quantum/matrix.c` and `quantum/matrix_common.c` (`builddefs/common_features.mk`). All three split `post_rules.mk` set `SPLIT_TRANSPORT = custom`, which skips `transport.c`, `transactions.c`, and serial drivers in that same `common_features.mk`. `quantum/split_common/split_util.c` still compiles and can call `transport_master()`; ERA provides no definition, so a caller would fail the link. `contracts/era_invariants.md` already requires that absence. QMK API / other keyboards / H7S stock copy are not ERA-delete. |
| KEEP-matrix-if | `transport_master_if_connected()` in `quantum/matrix.c` not linked | That file is not in RP2040 ERA images (`CUSTOM_MATRIX = yes`). Weak definition in `quantum/matrix.c`; split-util definition in `quantum/split_common/split_util.c` is unused by ERA's matrix engine. Not an ERA-only symbol to delete from QMK core. |
| KEEP-serial | `platforms/chibios/drivers/serial_usart.c` and `platforms/chibios/drivers/vendor/RP/RP2040/serial_vendor.c` not linked | `SPLIT_TRANSPORT = custom` as above. `serial_transport_receive_timeout()` remains declared in `platforms/chibios/drivers/serial_protocol.h`. `manuals/era_qmk_fork_ledger.md` already names that declaration as the carried QMK-user API. |
| KEEP-post-init | `era_split_keyboard_post_init()` in `split/era_split_keyboard.c` discards the bool from `era_split_transport_scheduler_start_communication_core()` | Deliberate. `contracts/era_invariants.md` and `maps/era_walkthrough.md`: failure handling lives inside the step. |
| KEEP-stage | Eight pre-`CORE1_FULL` stage macros with zero preprocessor readers | `split/era_split_qmk_rules.mk` lists six names in `ERA_SPLIT_COMMUNICATION_CORE_LEGACY_STAGE_VARS` and `$(error)`s if any is set. `ERA_SPLIT_COMMUNICATION_CORE_STAGE` must be `CORE1_FULL`. Zero `-D` of those six under `keyboards/era`, `quantum/`, `platforms/`, `drivers/`, `tmk_core/`. Keep the error list. |
| KEEP-wire-qwin | Wire + qwin together is not an `$(error)` | `build_variants/qwin.mk` says do not combine and forces wire off. `split/era_split_qmk_rules.mk` composes both if both are yes (shared counter `SRC`, skip a second counter object when qwin is on). Canonical variants use `override :=` so they never set both. Missing make-guard, not dead code. |
| KEEP-macro-diag | `era_via_macro_diagnostics.c` is in SRC under cause timeline, not `VIA_ENABLE` | `split/era_split_qmk_rules.mk` adds that `.c` under `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE` (which already requires wire + EEPROM sync + storage). Callers: `quantum/via.c` (same define), `era_split_keyboard.c` task, wire-diagnostics snapshot. Live on the cause variant. Source-map "no release image compiles the unit" is about release, not unused. |
| KEEP-nvm-quiet | `storage/era_nvm.c` names `ERA_STORAGE_QUIET_DEFER_MS` and does not `#ifdef` it | The comment points at `quantum/eeconfig.h` deferral of RGB toggle during macro upload. Readers of the constant are QMK core and `system/era_board_hooks.c`. Cross-file pointer, not a dead read. |
| KEEP-debounce-get | GET TIME_PRE returns TIME_SINGLE's value, so one id is dead | Both GET arms in `features/era_debounce_via.c` return `era_matrix_debounce_config_get_press_delay()`. SET TIME_SINGLE still writes the balanced pair. JSON presents MODE, TIME_SINGLE, TIME_PRE, TIME_POST. Live mapping. |
| KEEP-term-bands | one VIA term band is unused, or the two bands are a mismatch to resolve by dropping one | Owner 2026-08-31: both stay. `features/era_tapdance_via.c` answers legacy field ids 32–71 (term fields 36, 41, 46, 51, 56, 61, 66, 71) and exact-ms ids 72–79. `features/era_tapping_via.c` answers tapping channel GLOBAL_TERM and GLOBAL_TERM_EXACT. Tree JSON under `keyboards/era/**/keymaps/via/` presents `id_qmk_tapdance_*_term` and `id_qmk_tapping_global_term` — the stock-VIA / usevia.app legacy surface. The VIA app (`the-via-eerraa`) presents exact-ms. Intentional dual compatibility, not an unresolved mismatch. `tests/era_via_exact_ms/` proves the exact-ms round-trip. |

## Negative scans

- 85 `.c` files under `keyboards/era/common/{split,storage,system,features}/`
  are each named from some `SRC` / `QUANTUM_LIB_SRC` / `INTROSPECTION_KEYMAP_C`
  / test `.mk`.
- 93 `.h` files in those four trees are each named from some other unit.
- Every `keyboards/era/**/*.mk` fragment this sitting could see is included
  from `post_rules.mk`, split rules, or common rules (no orphan `.mk`).
- Every `tests/era_*` directory has `test.mk` or `test_variant_rules.sh` and
  is named from `maps/era_source_map.md` / `manuals/era_performance_gates.md`.
- Tracked tree: no graphify-out directory, no graphifyignore file, no graphify
  token. Workspace-only untracked `docs/era/` is not a git object and is not
  agent canon. This ledger does not add it.
- No persistence-burst design document. This ledger does not create one.

## Later-session order

One claim-cluster per later session. Do not merge clusters.

DC-empty-ifdef is not pending; see **Removed** above.
STALE-COMMENT is not pending; see **Corrected** above.

1. **Modes 5 and 6** — owner pick between the header sentence and
   `maps/era_identifier_map.md`. Not a deletion until that pick.
2. **Do not schedule** RI-due-bits, RI-result-full, RI-payload-4-6,
   RI-session-0x20, RI-owner-1, RI-snap-src-1, RI-queue-kind-1, RI-via-sync-old
   as deletes.
3. **PS-debounce-status** — only after the H7S / JSON / VIA pair is resolved.
4. **Do not delete** KEEP-transport / KEEP-serial / KEEP-matrix-if from this
   fork as "ERA dead code".
5. **Do not delete** KEEP-term-bands: both VIA term bands stay.

## What this sitting did not verify

- No UF2 build, no `nm`/`size` on an ELF (make-rule and invariant proof only
  for serial and `transport_*` absence).
- No device capture.
- H7S and VIA were read at the SHAs above only; they were not edited; their
  other unused surfaces are out of scope.
