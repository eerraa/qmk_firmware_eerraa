# ERA Performance Gates

Genre: manual
Canonical for: what a change owes before it is believed — the build, the source
and static checks, the refactor tiers, and every standing figure a change must
not regress

## The Instruments Are In The Tree And Off In Release

The wire diagnostics, qwin scan-rate window, pass-phase itemiser and
storage/VIA cause instruments sit behind selectors
`keyboards/era/era_build_options.mk` declares `?= no`. Every selector off
yields the byte-identical release UF2. Switching an instrument on is a build
option, never a re-instrumentation. Variants: `era_build_options.md`,
`era_build_and_flash.md`.

1. **Select the common build variant.** `era-build keyboard:keymap wire`,
   `qwin`, `cause`, `stale` or `qwin_phase` applies
   `keyboards/era/common/build_variants/`. Make refuses a variant whose
   prerequisites the target lacks. The launcher refuses unless the resolved
   five-axis tuple agrees with the link-visible ELF witnesses, before writing
   an artifact or manifest.
2. **Bind a keycode.** `WIRE_DIAG`, `WIRE_DIAG_2` and `WIRE_QWIN` exist in
   the family enum; **the shipped keymaps bind none of them**. Bind one
   temporarily, measure, restore the keymap.
3. **Read what it prints** with `era_capture_reading.md`.

**Do not infer a figure from source and record it as measured.** The
**Fixed Baselines** below were taken with these instruments.

## What A Change Owes

Scoped by what the change touched. A change that touches no source owes none
of it — saying so is the verification statement.

| The change… | owes |
| --- | --- |
| touches any source | a build through `era-build keyboard:keymap`, whose first step synchronises the WSL local build tree to the change |
| claims to touch no behaviour | the **Refactor Self-Check** tier its binary earns |
| touches a closed surface, a retired path, or a QMK core matrix file | the **Source Gate** |
| touches RAM placement or the load image | the **Layout Checks**, per board |
| adds core1 call depth | a measured stack figure — no compile-time construct can report one |
| changes replacement Apply, the custom EEPROM adapter, ERA NVM, or dynamic-macro durability | `era_nvm`, `era_nvm_qmk_driver`, the focused storage/State-Sync/CLEAN set below, and a supported split build |
| restores or changes QMK wear-level files | the complete upstream wear-level host-test set below, proving the restored QMK implementation remains stock-compatible |
| changes scheduler request admission, queue freshness, or Core1 liveness | the scheduler admission/liveness device gate below at every supported link level |
| changes EEPROM CLEAN's reboot-durable prepare, agreed-restart phase machine, or storage quarantine | a deterministic CLEAN state-machine host regression covering physical-replay failure, PREPARE/COMMIT/echo loss and duplication, relation rotation and quarantine; a supported split build; and the EEPROM CLEAN agreement device gate below |
| changes common build-variant rules or the launcher identity | the variant-precedence test, all six canonical variants on one representative split target, and Brick65 `standard`; each manifest must carry equal resolved/compiled tuples |
| would move a **Fixed Baseline** | an instrument, first |

The build itself — what WSL must provide, the automated command, and its
refusals as stop conditions — is `era_build_and_flash.md`'s.

### Focused host-test set

Run these in the WSL-local tree after the build automation has synchronized it:

```text
make test:era_nvm
make test:era_nvm_qmk_driver
make test:era_host_peer_storage_recency_policy
make test:era_host_peer_storage_indicator_policy
make test:era_host_peer_storage_standing_policy
make test:era_split_responder_result_policy
make test:era_split_restart_agreement
make test:era_split_storage_publication_retire
make test:era_via_exact_ms
make test:era_rgb_matrix_persistence
make test:wear_leveling_general
make test:wear_leveling_2byte_optimized_writes
make test:wear_leveling_2byte
make test:wear_leveling_4byte
make test:wear_leveling_8byte
make test:era_rp2040_matrix_pio
bash tests/era_build_variant_rules/test_variant_rules.sh
```

| Test | Pins |
| --- | --- |
| `era_nvm` | production A/B power-cut/fault: mount and generation authority, append commit ordering, 16-KiB atomic replacement, mandatory rotation, one-sector inactive maintenance, format, macro staging, CLEAN replay |
| `era_nvm_qmk_driver` | ERA custom adapter compiled with stock QMK EEPROM helpers and stock `nvm_dynamic_keymap.c`: ordinary read/write/update, exact committed-span notification, non-notifying durable internal storage metadata, faulted counter writes that keep the previous public/replay value, the atomic counter-through-baseline convergence envelope, the real 16-byte stock macro RESET transcript, deferred RGB write inside an open macro, macro-touching refusal, failed close, whole-store erase, CLEAN replay, physical prepare failure |
| `era_host_peer_storage_recency_policy` | failed increment/clear cannot publish a settled capture or signal departure; failed convergence-metadata publication cannot retire recency. Tests the production policy header; does not construct a second fake HOST-PEER runtime |
| `era_host_peer_storage_indicator_policy` | a serviced relation and the initiator's finite fast-recovery window both preserve the peer-pending mirror; a backed-off no-link state retires it. A successfully sent one stays visible-pair work after the local semantic arm falls until that role's sent-state boundary confirms zero; a one that never crossed creates no synthetic hold; a closed local gate cannot latch a new sent-one obligation and does not erase an already-confirmed one before its zero crosses. The scheduler owns whether the recovery window is active; the test clones neither scheduler nor storage runtime |
| `era_host_peer_storage_standing_policy` | standing cadence remains admitted in ordinary service and is suppressed by either transfer exclusivity or the push initiator's remote-responder Apply wait. The latter does not widen route exclusivity or responder-result coalescing |
| `era_split_responder_result_policy` | responder general-ring capacity. Coalesce only an exact successful section-bearing HEARTBEAT already represented by the same immutable responder snapshot/mask; reject empty, changed-generation/mask, failed/unsent, SESSION and both push result kinds. A synchronous Core0 Apply can outlast all three usable general slots while Core1 keeps answering |
| `era_split_restart_agreement` | PREPARE/COMMIT/echo loss and duplication, relation rotation, sticky prepare failure, quarantine |
| `era_split_storage_publication_retire` | production publication unit: CLEAN terminal sentinel, ready/unclaimed discard and active claim drain on both storage directions; nonterminal relation-loss discard that clears ready result ownership and leaves source publication capacity reusable; an in-flight non-ready reservation stays owned until Core1 publishes it ready, then the same discard closes it on a later Core0 pass |
| `era_via_exact_ms` | State Sync envelope and revision boundaries |
| `era_rgb_matrix_persistence` | deferred RGB Matrix save gate and ERA render-policy refresh. A policy edge stays refresh-active from its external request through the replacement PWM flush (or until the policy proves no frame is needed), which keeps opportunistic NVM bank erasure out of a split STATUS transition |
| wear-level set | the four `wear_leveling_*` targets prove the QMK files restored at cutover behave like stock QMK again; they are not ERA production persistence |
| `era_rp2040_matrix_pio` | SRAM-resident execution assumptions the storage cutover shares |
| `test_variant_rules.sh` | all six tuples via direct assignments, `MAKEFLAGS`, exported environment and `make -e`; non-split standard-only refusal |

A make-only variant test is not the launcher. Only the launcher derives
`compiled_tuple` from the ELF and binds that result to artifact and manifest
identity.

### Scheduler admission/liveness device gate

| | |
| --- | --- |
| Entry | the same storage-changing workload in both DUAL-HOST directions and with each physical half taking the HOST-PEER PEER role, at High, Medium and Low, on a `cause` image |
| Pass | `QUEUE_EXPIRED`=0, storage core `fail`=0, `claim == tx == rx == publish`; Core1 dead/cap/reclaim/service-failure counters do not advance; each operation advances storage transfer/complete/apply exactly once; abort, storage timeout, integrity, version, domain and queue-publication failures stay 0 |

The scale matrix is mandatory: queue freshness is derived from the runtime
wire scale, while the lifecycle progress word advances only after the
selected non-preemptive service returns
(`split/scheduler/era_split_transport_scheduler_routes.c`,
`split/scheduler/era_split_transport_scheduler_timing.c`,
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`).
A scale-one static expression cannot prove Medium/Low service occupancy.

### ERA NVM persistence device gate

Run after the host fault suite and supported build pass. Source page counts
are capacity evidence only; **no numeric speedup is accepted without these
device measurements.** Compact before/after signatures from the existing
wire/storage diagnostics; record wall-clock operation time separately.

| Leg | Measure |
| --- | --- |
| 16-KiB dynamic-macro upload | opener through durable zero close and targeted marker readback |
| journal-room Apply | 16-KiB replacement whose active journal has room and therefore does not rotate |
| rotation Apply | bank rotation, remaining inactive-bank sector erases, new-bank construction |
| physical diagnostic deltas | program and erase totals may move; `program_failure_count` and `erase_failure_count` stay 0 in every healthy run |
| post-close agreement | content/readback match and exactly the expected KEYMAP/MACRO/CONFIG State Sync revision boundary |
| relation liveness | Core1 failure, storage timeout, integrity, stale and queue-expiry counters do not increase outside the explicitly exercised failure leg, across the complete synchronous NVM window |
| no scan/wire regression | outside the accepted synchronous flash window |

Exercise both storage directions and both physical roles under the scheduler
gate's relation matrix. At minimum take normal-journal and rotation timing on
High after both directions pass; repeat at Low where the scheduler gate
requires the storage workload. A rotation-triggering macro upload may use the
more expensive open-then-append shape currently implemented. Do not optimize
or normalize that measurement away: it is the evidence required before
deciding whether to fold the candidate into the rotating snapshot.

### EEPROM CLEAN agreement device gate

Controlled software-reset only; RUN/DVDD reset and power cut are not legs.
Entry: identical non-default layout and macro on both halves; High DUAL-HOST
both command directions and HOST-PEER with each half as PEER; then the two
DUAL-HOST directions at Low after High passes.

For every serviced CLEAN, compact phase evidence must order one REQUEST,
reboot-durable local PREPARED on both halves, one COMMIT carrying a nonzero
common deadline, responder COMMIT_ARMED, and the initiator's adoption of
that same deadline. Neither half may hold a commit deadline before both
PREPARED states, and no storage claim, transmit, receive, capture, transfer,
Apply or responder admission may begin after the first PREPARED and before
reset. On each half's live `wire clean` lines, `cq` and `hs` remain
bit-for-bit unchanged from event 4 through its last pre-reset event;
request/result/ready generations and storage-active are already zero at
event 4. Both halves disconnect and boot; ordinary boot mount recovers
MAGIC_OFF; QMK's init path formats ERA NVM and rebuilds defaults; then
relation-open convergence closes all seven domains as MATCH with `xfer=0`
and `apply=0`. Storage abort, timeout, integrity, version, domain,
queue-publication and Core1-failure counters stay 0; the indicator ends on
both halves with `vis=0` and `pnd=0`.

| Host-regression leg | Must hold |
| --- | --- |
| physical replay or either local invalidation failing | PREPARED unpublished; no deadline or reset; quarantine sticky |
| lost or duplicated PREPARE | idempotent; cannot create a deadline |
| lost, duplicated or delayed COMMIT/echo | cannot consume PREPARED as COMMIT_ARMED or reopen quarantined storage |
| lost COMMIT echo then relation rotation | must cross canonical idle before a fresh PREPARED observation can create a replacement deadline |
| every rotation | rejects stale peer phase; retains and re-drives the local monotonic prepare obligation |
| refused act/param/bit7/deadline tuples | all refused |
| envelope | five-byte RESTART_ARM and seven-byte AUTHORITY bodies, masks, eligibility bytes and standing cadence do not move |

These host legs are coverage a device run must not manufacture.

## Refactor Self-Check

A change whose whole claim is "this touches no behaviour" owes a check that
says so. The tiers classify a change by **what its binary did**.

| Tier | What the change is | The check | A pass means |
| --- | --- | --- | --- |
| **T1** | comments and documents; renames; deleting code the linker already discarded | `uf2_sha256` identical | the change provably touched no behaviour |
| **T2** | file splits, moving functions between units, header reorganisation | `nm -S` symbol set *and* per-symbol size identical; section totals unchanged; addresses may shift | the instructions are the same, at different addresses |
| **T3** | deduplication, control-flow change, struct reshaping | the ELF delta is explained and matches what the commit claims | the change did what it said and no more |

**The tier is a result, not a judgment call.** Propose the tier, run the
check, and let the check decide. A T1 or T2 pass outranks any device reading
of the same question and is never waived for cost.

**T1's identity holds within one build day.** `via_eeprom_set_valid()`
(`quantum/via.c`) derives the VIA EEPROM magic from `QMK_BUILDDATE`'s year,
month and day, so the same source rebuilt on another day differs in exactly
three bytes — the day nibbles where that magic is written and compared — and
in nothing else. Compare across a day boundary by pinning `--build-date` to
the earlier day, or by reading a `uf2_sha256` mismatch made of exactly those
three bytes (a T1 pass, not a T3). The first boot of an image built on a new
day finds the stored magic stale and resets VIA's region.

- **When a change moves a mechanism an active document names, that text is
  updated in the same commit.**
- **A tier pass is evidence about a binary, never a substitute for coverage.**

## Source Gate

| Check | Accept |
| --- | --- |
| matrix files | `quantum/matrix_common.c` byte-identical to upstream. `quantum/matrix.c` differs by both departures `era_invariants.md` admits — the two `MATRIX_SCAN_{RAW,COUNT}_DIAGNOSTICS_ENABLE` hooks with the time helpers and include block they need, *and* the `debounce`/`matrix_post_scan` split — and by nothing else. Uncommitted: `git diff c93ef27143 -- quantum/matrix.c`; after commit the `HEAD` form must agree. **Never compare against another ERA branch.** Upstream-base spelling: `era_qmk_fork_ledger.md` |
| absence | no dynamic allocation, Pico SDK queue, stock split serial backend, or synchronous core0 ERA wire fallback is linked |
| replacement Apply | no alternate public EEPROM view; ADMIT / old-or-new / repair-forward order is `era_host_peer_storage_contract.md` |
| variant identity | every automated build emits exactly one unique resolved variant/tuple pair after make restarts are deduplicated; requested and resolved names match; resolved and compiled tuples match; artifact stem and manifest use that resolved name; a non-split ELF admits only the all-`no` `standard` tuple |
| USB master pair | no `usb_disconnect`, `usb_connected_state`, or `usb_vbus_state` symbol is linked, and `is_keyboard_master_impl` resolves to the ERA override. If any of those symbols reappears, the stock boot-master poll has been relinked |
| attach | no USB attach-gate or `startup_connect` symbol is linked |
| core ownership | Core1 / Core0 ownership of backends, policy, live QMK state and apply is `era_authority_contract.md` / `era_invariants.md`. Core1 has no live matrix, RGB, VIA, EEPROM, host LED, USB or HID read and no EEPROM write. Bare core1 and its PIO IRQ path call no ChibiOS thread suspend/resume or scheduler-lock API |
| capacity before success | no success response that introduces new Core0 work is sent before required result capacity is reserved and accepted source-push/chunk data is copied. An exact duplicate successful HEARTBEAT may reuse an already-pending result only under that invariant; SESSION/push or a changed snapshot/mask cannot |
| DUAL-HOST eligibility | not an absence check — both relations run the same engine and reuse the `0x20`/`0x21` envelope pair. Three parts: exactly one send site and one admission site per direction AND against the linked eligibility table, with no other code opening a section; the ELF check below reading that table's bytes; a `_Static_assert` beside it forbidding an entry that names a section outside the landed set. **Review the two AND sites, not a symbol list.** |

## ELF And Static-Capacity Checks

Targeted `arm-none-eabi-size` / `nm` / `objdump`. No aggregate
topology-script acceptance surface.

**Three checks are enforced rather than inspected, in
`common/tools/era_residency_gate.sh <elf>`**: 32 KiB free-ram0 floor (value
in `era_sram_residency_contract.md`), allocator absence, and the
vector-table check. ELF in, three manifest fields out. The launcher calls it
for every UF2. Every `.vectors` entry except the reset slot must hold an
SRAM address; the reset slot still holds a flash one (boot2 fetches the
reset PC from flash). Read the linked table's own bytes, not handler names.
The linker `ASSERT` on `era_unhandled_vector` is the complement: it catches
only the whole object going missing. Canonical here per `era_invariants.md`.

Every ERA split link emits a `.map`. Review `linker stubs`, not a `veneer`
name grep: two expected 16-byte stubs in `.flash_startup` serve the
post-copy `__late_init`/`main` calls; one 16-byte stub in `.sram_image` is
the post-copy SRAM→flash call into the pinned board object; a **new** stub
inside `.flash_startup` is a pre-copy caller reaching into `.sram_image`.

| When the change reaches… | Instrument |
| --- | --- |
| `.text`/`.data`/`.bss` movement; linked stack reservations; wire formatter frames and nested core0 depth against `__process_stack_size__` | `size` / `nm` / `objdump` |
| added core1 call depth | `common/tools/era_core1_stack_walk.py <elf>` walks the deepest frame sum reachable from `era_split_communication_core_entry()` (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`) along `bl` edges, reports recursion rather than following it, and adds the 32-byte Cortex-M0+ exception frame. Do not hand-walk. Compare the total against the linked reservation; headroom is the acceptance, a delta is not |
| matrix, RGB, timer, backend and storage symbol placement; calls that escape an accepted RAM island into XIP; core1 storage undefined/call symbols for live EEPROM/QMK/VIA/RGB/matrix/USB/HID access | `nm` / `objdump` |
| stock serial, removed V16, core0 executor and retired stage symbols | absence. `DUAL_RUNTIME_BUNDLE` is a **permanent** absence check |
| section eligibility matrix | `arm-none-eabi-nm` for `g_era_split_wire_section_eligibility`, then `arm-none-eabi-objdump -s -j .sram_image` over its ten bytes: two per relation mode in `era_split_mode_planner.h` order, push then response. Current expected reading: `00 00 / C5 FC / C5 FC / FE F7 / FE F7`. One current value, never a series. Re-derive from `era_split_wire_protocol.h`'s five `ELIGIBLE_*` masks in the same commit that opens a cell. Which relation may carry which section: `era_wire_contract.md` |
| storage summary response time-anchor seat | still reads zero, validator-enforced, **permanently** — the anchor has one carrier in every relation |
| actual storage records and alignment | against the aggregate static cap by equality; the cap's value is `era_host_peer_storage_contract.md` |
| automatic frame sizes | no bulk/result record consumes core1 or process stack |

## Layout Checks

RAM placement, load-image scope and budget:
`era_sram_residency_contract.md`. Per board. The live instrument is
`common/tools/era_residency_gate.sh` (SRAM budget, allocator, `.vectors`);
it uses neither Layout phrase. That contract still writes this procedure as
Layout Gate; the name here is **Layout Checks**.

| Check | Accept |
| --- | --- |
| mixing | do not change layout while functional source is changing |
| enforced gate | `era_residency_gate.sh` on each keymap's ELF for the three enforced checks |
| objdump/nm | every ALLOC section VMA in ram0/ram4/ram5 except boot2 and the flash LMA image; vectors LMA at exactly `0x10000100`; flash startup carve-out closed; no linked symbol outside that carve-out fetched from XIP after crt0; the load image's four redefined init/fini array symbols present; veneer map on a first link |
| double-tap magic | not accepted from an inferred placement. Confirm `magic_location == __bss_end__ == __era_bootloader_magic_base__` and `__era_bootloader_magic_end__ - __era_bootloader_magic_base__ == 4`. Why the word survives a reset is the residency contract's. Disassemble `early_hardware_init_pre()` (`system/era_boot_core1_halt.c`) and require no call into `.sram_image`; the flash carve-out permits only the two established post-copy crt0 veneers |
| smoke | both-orientation storage, typing, RGB and recovery smoke on the exact selected load image, plus a fresh no-cable qwin comparison point. Scan rates are never compared across a layout change |

## Fixed Baselines

**These are the comparison points a later change is judged against, and this
is their one home.** Quote them from here and nowhere else. Each was taken
with an instrument this tree still carries.

| Baseline | Value | Device grounds | Instrument |
| --- | --- | --- | --- |
| Tomak RP2040 row-settle | 128 CPU cycles | `32` and later `64` each produced false local matrix input while typing. Any lower value or board override stays rejected until a separate electrical settle-margin test proves otherwise | matrix / typing |
| Split USART | 460800 baud | `576000` and `748800` produced source-push ACK gaps under high-rate typing; stay rejected until separate signal-margin work proves otherwise | wire / typing |
| Performance floor | 18 kHz on each half, cable or no cable (owner decision 2026-08-01) | lowest cabled-rig window ever recorded is `21463` | qwin |
| PIO sampler frame rate | `smp_hz=80129` against designed 80128 (12.48 µs frame) | `ovr=0 rearm=0 fd1=0` on every window; any of them non-zero is a finding | qwin / PIO |
| Render chunk | `c + LEDs × e`; **c ≈ 8–11 µs** a chunk; **e ≈ 2.97 µs** an LED on a non-reactive effect; **e ≈ 14.9 µs** on the heaviest reactive effect (five-fold in `e`, `c` unchanged) | the fixed term is the per-chunk policy update, effect dispatch and `rgb_matrix_indicators_advanced()` call in `quantum/rgb_matrix/rgb_matrix.c`, so it does not shrink when the chunk does. Worst chunk `c + L·e` falls linearly in `L` while the frame's fixed cost `ceil(n/L)·c` rises as `1/L`. Between the shipping four and a hypothetical three the marginal price of a microsecond of worst case rises **six-fold** | RGB / chunk |
| RGB frame rate | 62.34 fps with the idle gate, against 62.50 without it | the 0.26 % is the gate sampling a millisecond clock at a period one pass longer than a millisecond, and is the whole of the mechanism's behavioural residue. **It does not reach animation speed**: effect phase is computed from `g_rgb_timer` | RGB |

Changing any fixed baseline requires its own controlled real-device gate.

### What the shipped firmware measures, on TOMAK79H

Taken on 80–110 s windows, two per configuration, every window flat:

| connection | half | scan rate | pass | ratio to no cable |
| --- | --- | ---: | ---: | ---: |
| no cable (LOCAL_NO_LINK) | LEFT | **56,190** | 17.794 µs | — |
| no cable (LOCAL_NO_LINK) | RIGHT | **55,516** | 18.003 µs | — |
| **HOST-PEER, this half the HOST** | LEFT | **54,924** | **18.210 µs** | **97.72 %** |
| DUAL-HOST, this half the initiator | LEFT | **54,841** | 18.233 µs | 97.59 % |
| DUAL-HOST, this half the responder | RIGHT | **54,052** | 18.499 µs | 97.32 % |

**HOST-PEER is the baseline performance configuration and DUAL-HOST is the
option** (owner decision) — a HOST-PEER figure outranks a DUAL-HOST one
wherever the two disagree. Both cost gates pass on every row: the lowest
window clears the floor by a factor of 3.0, and every cabled ratio clears
95 % with at least 2.3 points to spare.

- **The relation costs a HOST's core0 0.415 µs a pass**, 2.28 % of it; the
  DUAL-HOST pair reads 0.438 (initiator) and 0.497 (responder). **The
  responder pays more than the initiator**, recorded twice.
- **A cabled figure reproduces about ten times worse than a no-cable one, and
  a power cycle is enough to show it** — 0.075 µs apart across a power cycle
  against 0.005 for two no-cable windows in the same sitting. **Take a cabled
  figure twice, in two sittings, before believing a change of less than
  0.1 µs in it.**
- **RIGHT runs 1.16 % below LEFT with no cable, and that is the halves' LED
  counts** — 55 against 41 is thirteen more render chunks and fourteen more
  LEDs a frame; the chunk constants predict +0.18 µs a pass against +0.21
  observed.

**The gate tightens as the pass gets faster, with no relation work at all.**
The relation costs 0.31–0.45 µs absolute, which is 1.5–2.2 % of an 18 µs
pass and would have been 0.7–1.0 % of the 44.9 µs pass this firmware started
from. A figure quoted as "the relation costs X %" is meaningless without the
pass it was taken on.

**Two core1 figures any later core1 work is argued against**: the initiator's
core1 **sleeps 84.0 % of a cabled window**, the responder 89.1 %, which is
what says the pass shortening is core0's alone. The PIO sampler's consumer
costs **3.18 µs a pass**.

**The core1 stack is 912 B used on `standard`, `qwin` and `qwin_phase`,
992 B on `wire` and `stale`, and 1000 B on `cause`, against a 2048 B
reservation**, exception frame included — 1048 B headroom in the worst
image. `cause` is eight bytes deeper (selector-only pending-path probe).
**Per variant; a reading that does not name its image is not a reading.**
Re-derive with `common/tools/era_core1_stack_walk.py <elf>` on the ELF
beside the variant's UF2. A change that adds core1 call depth re-runs the
walk.

## The Split-Relation Cost Gates

**Two gates, side by side, and a candidate must pass both** (owner decision
2026-08-01). Either alone admits a failure the other catches.

| Gate | Value | The failure it catches |
| --- | --- | --- |
| **Floor** | ≥ 18 kHz, cable or no cable | the keyboard became slow to use, cause unexamined |
| **Target** | cabled ÷ **same-image** no-cable ≥ **95 %** | the split relation is eating core0 |

**Compute the target per half, never as one figure for the pair.** **The
target is a ratio and must not be written as an absolute.** No-cable scan
rate reaches ~20 kHz here, so an absolute at the ceiling reads noise as
failure. A ratio alone cannot catch two numbers falling together: 13.5 /
14 kHz is 96 % and a slow keyboard.

- **Nobody knows whether 5 % is a large margin or a small one.** (owner)
- **Below an 18.95 kHz ceiling the ratio stops binding**, because 95 % of it
  falls under the floor. Do not cite "the ratio passed" as evidence there.

**The ownership rule that once kept RGB on core0 is withdrawn.** What
replaces it: **core0 owns the path from a switch to a HID report, and core1
owns presentation.** The withdrawal does not license three things: it is not
a claim that rendering is cheap to move; it does not move the floor or the
target; it does not make the ceiling a target. **Reopening the rule needs an
owner decision, not a measurement.**

## Evidence Storage

A new measurement replaces the figure in **Fixed Baselines**, never a
series. Raw-log rule: `AGENTS.md`.
