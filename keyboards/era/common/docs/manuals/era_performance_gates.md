# ERA Performance Gates

Status: active
Genre: manual
Canonical for: what a change owes before it is believed — the build, the source
and static checks, the refactor tiers, and every standing figure a change must
not regress
Read when: changing scan-bound work, RAM placement, scheduler behaviour,
communication-core ownership, or HOST-PEER storage

**This document is forward-looking: it says what a *new* change must
demonstrate.** It carries no record of which checks a past change ran, and it is
not a campaign playbook: the development campaign that produced this firmware is
finished, so the per-leg sequences it ran retired with it. What survives is what
a change still owes, the **Fixed Baselines** it must not regress, and how to
turn the instruments back on.

## The Instruments Are In The Tree And Off In Release

**The console diagnostics are kept, compiled out, and one selector away.** The
  wire diagnostics, the qwin scan-rate window, the pass-phase itemiser and the
  storage/VIA cause instruments are all still here, every one of them behind a selector
that `keyboards/era/era_build_options.mk` declares `?= no`. A release build
compiles none of it — provable rather than asserted, because turning every
selector off yields the byte-identical UF2 the release build already produces —
and the counters they read live in the ordinary state records, so switching an
instrument on is a build option and never a re-instrumentation.

What a measuring session does, in order:

1. **Select the common build variant.** `era-build keyboard:keymap wire`,
   `qwin`, `cause`, `stale` or `qwin_phase` applies the repository-owned
   combination from `keyboards/era/common/build_variants/`; the make layer
   refuses a variant whose prerequisites the selected target lacks. The
   launcher also refuses unless the resolved five-axis tuple agrees with the
   link-visible ELF witnesses, before it writes an artifact or manifest.
2. **Bind a keycode.** `WIRE_DIAG`, `WIRE_DIAG_2` and `WIRE_QWIN` exist in the
   family enum on every build, and **the shipped keymaps bind none of them** —
   a shipping keyboard may not spend a key on an instrument that does nothing in
   a release image. Bind one temporarily, measure, restore the keymap. That edit
   is part of taking a capture, not a change to the firmware.
3. **Read what it prints** with `era_capture_reading.md`, which is the decode:
   every field's meaning, which counters are totals against rates, and the
   reading traps that decide whether a reading is valid at all.

**The Fixed Baselines below were taken with those instruments**, which is why
they can be re-taken. **Do not infer a figure from source and record it as
measured** — the rule that device evidence outranks source inference
(`AGENTS.md`) applies with full force precisely because the measurement is
available.

## What A Change Owes

Scoped by what the change touched, and a change that touches no source owes none
of it — saying so is the verification statement.

| The change… | owes |
| --- | --- |
| touches any source | a build through `era-build keyboard:keymap`, whose first step synchronises the WSL local build tree to the change |
| claims to touch no behaviour | the **Refactor Self-Check** tier its binary earns |
| touches a closed surface, a retired path, or a QMK core matrix file | the **Source Gate** |
| touches RAM placement or the load image | the **Layout checks**, per board |
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
make test:era_split_restart_agreement
make test:era_split_storage_publication_retire
make test:era_via_exact_ms
make test:wear_leveling_general
make test:wear_leveling_2byte_optimized_writes
make test:wear_leveling_2byte
make test:wear_leveling_4byte
make test:wear_leveling_8byte
make test:era_rp2040_matrix_pio
bash tests/era_build_variant_rules/test_variant_rules.sh
```

`era_nvm` is the power-cut/fault proof of the production A/B format: mount and
generation authority, append commit ordering, 16-KiB atomic replacement,
mandatory rotation, one-sector inactive maintenance, format, macro staging and
CLEAN replay semantics. `era_nvm_qmk_driver` compiles the ERA custom adapter
with stock QMK EEPROM helpers and stock `nvm_dynamic_keymap.c`; it pins ordinary
read/write/update behavior, exact committed-span notification, non-notifying
durable internal storage metadata, faulted counter writes that keep the previous
public/replay value, the atomic counter-through-baseline convergence envelope,
the real 16-byte stock macro RESET transcript, the deferred RGB write inside an
open macro, macro-touching refusal, failed close, whole-store erase, CLEAN
replay proof and physical prepare failure. `era_via_exact_ms` owns the State Sync envelope and
revision boundaries. The upstream wear-level set is intentionally unrelated to
ERA production persistence now: it proves the QMK files restored during the
cutover behave like stock QMK again.

`era_host_peer_storage_recency_policy` is the narrow state-boundary proof beside
those physical NVM fault tests: a failed increment/clear cannot publish a
settled capture or signal departure, and a failed convergence metadata
publication cannot retire recency. It tests the small production policy header
rather than constructing a second fake HOST-PEER runtime.

`era_split_storage_publication_retire` runs the exact production publication
unit and proves CLEAN's terminal sentinel, ready/unclaimed discard and active
claim drain on both storage directions, plus the nonterminal relation-loss
discard that clears ready result ownership while leaving source publication
capacity reusable by the next relation; an in-flight non-ready reservation is
left owned until Core1 publishes it ready, then the same discard closes it on a
later Core0 pass. `era_split_restart_agreement` pins
PREPARE/COMMIT/echo loss and duplication, relation rotation, sticky prepare
failure and quarantine. Matrix PIO remains in the set because the storage
cutover touches the SRAM-resident execution assumptions it shares.

The make-only variant test attacks all six tuples through direct assignments,
`MAKEFLAGS`, exported environment and `make -e`, and checks the non-split
standard-only refusal. It does not replace firmware builds: the launcher is the
only check that derives `compiled_tuple` from the ELF and binds that result to
artifact and manifest identity.

### Scheduler admission/liveness device gate

Run the same storage-changing workload in both DUAL-HOST directions and with
each physical half taking the HOST-PEER PEER role, at High, Medium and Low with
a `cause` image. At each level, `QUEUE_EXPIRED` and storage core `fail` remain
zero, `claim == tx == rx == publish`, and the Core1
dead/cap/reclaim/service-failure counters do not advance. Each operation still
advances storage transfer/complete/apply exactly once, with abort, storage
timeout, integrity, version, domain and queue-publication failures at zero.
The scale matrix is mandatory because queue freshness is derived from the
runtime wire scale, while the lifecycle progress word advances only after the
selected non-preemptive service returns; a scale-one static expression cannot
prove the Medium/Low service occupancy (`split/scheduler/era_split_transport_scheduler_routes.c`,
`split/scheduler/era_split_transport_scheduler_timing.c`,
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`).

### ERA NVM persistence device gate

Run this after the host fault suite and supported build pass. Source page counts
are capacity evidence only; **no numeric speedup is accepted without these
device measurements.** Use compact before/after signatures from the existing
wire/storage diagnostics and record the wall-clock operation time separately.

Measure at least:

- a complete 16-KiB dynamic-macro upload, opener through durable zero close and
  targeted marker readback;
- a 16-KiB replacement Apply whose active journal has enough room and therefore
  does not rotate;
- a replacement Apply that requires a bank rotation, including any remaining
  inactive-bank sector erases and new-bank construction;
- the NVM physical diagnostic deltas for those operations: program and erase
  totals may move, while `program_failure_count` and `erase_failure_count` stay
  zero in every healthy run;
- post-close content/readback agreement and exactly the expected KEYMAP/MACRO/
  CONFIG State Sync revision boundary;
- relation standing liveness across the complete synchronous NVM window, with
  Core1 failure, storage timeout, integrity, stale and queue-expiry counters not
  increasing outside the explicitly exercised failure leg;
- no scan/wire regression outside the accepted synchronous flash window.

Exercise both storage directions and both physical roles under the relation
matrix required by the scheduler gate. At minimum the normal-journal and
rotation timing must be taken on High after both directions pass; repeat at Low
where the scheduler gate requires the storage workload. A rotation-triggering
macro upload may use the more expensive open-then-append shape currently
implemented. Do not optimize or normalize that measurement away: it is the
evidence required before deciding whether to fold the candidate into the
rotating snapshot.

### EEPROM CLEAN agreement device gate

This gate exercises the controlled software-reset contract; an external
RUN/DVDD reset or power cut is deliberately not a leg. Start with identical
non-default layout and macro content on both halves. At High, run DUAL-HOST in
both command directions and HOST-PEER with each physical half taking PEER;
repeat the two DUAL-HOST directions at Low after High passes.

For every serviced CLEAN, the compact phase evidence must order one REQUEST,
reboot-durable local PREPARED on both halves, one COMMIT carrying a nonzero common
deadline, responder COMMIT_ARMED, and the initiator's adoption of that same
deadline. Neither half may hold a commit deadline before both PREPARED states,
and no storage claim, transmit, receive, capture, transfer, Apply or responder
admission may begin after the first PREPARED and before reset. On each half's
live `wire clean` lines, `cq` and `hs` remain bit-for-bit unchanged from event
4 through its last pre-reset event;
request/result/ready generations and storage-active are already zero at event
4. Both halves must disconnect and boot; the ordinary boot mount must recover
MAGIC_OFF and QMK's init path must format ERA NVM and rebuild defaults, then
relation-open convergence closes all seven domains as MATCH with `xfer=0` and
`apply=0`. Storage abort, timeout, integrity, version,
domain, queue-publication and Core1-failure counters remain zero; the indicator
ends on both halves with `vis=0` and `pnd=0`.

The deterministic host regressions supply the destructive failure legs a
device run must not manufacture: physical replay or either local invalidation failing leaves
PREPARED unpublished and creates no deadline or reset while quarantine remains
sticky; lost or duplicated PREPARE is idempotent and cannot create a deadline;
lost, duplicated or delayed COMMIT/echo cannot consume PREPARED as
COMMIT_ARMED or reopen quarantined storage; a lost COMMIT echo followed by
relation rotation must cross canonical idle before a fresh PREPARED observation
can create a replacement deadline; and every rotation rejects stale peer phase
while retaining and re-driving the local monotonic prepare obligation. It also
proves all refused act/param/bit7/deadline tuples and that
the existing five-byte RESTART_ARM and seven-byte AUTHORITY bodies, masks,
eligibility bytes and standing cadence do not move.

## Refactor Self-Check

A change whose whole claim is "this touches no behaviour" owes a check that says
so, not a build that survived. The tiers classify a change by **what its binary
did**.

| Tier | What the change is | The check | A pass means |
| --- | --- | --- | --- |
| **T1** | comments and documents; renames; deleting code the linker already discarded | `uf2_sha256` identical | the change provably touched no behaviour |
| **T2** | file splits, moving functions between units, header reorganisation | `nm -S` symbol set *and* per-symbol size identical; section totals unchanged; addresses may shift | the instructions are the same, at different addresses |
| **T3** | deduplication, control-flow change, struct reshaping | the ELF delta is explained and matches what the commit claims | the change did what it said and no more |

**The tier is a result, not a judgment call**, which is what makes the check
self-policing rather than a label a commit awards itself. A split that changes
an inlining decision moves a symbol size, so it fails T2 and *is* a T3 — it owes
an explanation instead of failing a gate. The same shape catches a deletion:
removing one caller of a two-caller file-scope static makes the compiler inline
the remainder, so an ELF-neutral-looking deletion can still move RAM, which has
happened twice and read as a mistake both times without the explanation. Propose
the tier, run the check, and let the check decide.

**A T1 or T2 pass outranks any device reading of the same question**, which is
why neither is ever waived for cost. A capture says no difference was observed;
these say there is none.

**T1's identity holds within one build day.** `via_eeprom_set_valid()`
(`quantum/via.c`) derives the VIA EEPROM magic from `QMK_BUILDDATE`'s year,
month and day, so the same source rebuilt on another day differs in exactly
three bytes — the day nibbles where that magic is written and compared — and in
nothing else. Compare across a day boundary either by pinning `--build-date` to
the earlier day, or by reading the byte diff and finding only those three; a
`uf2_sha256` mismatch made of exactly them is a T1 pass, not a T3. What the day
byte does on the device is a separate, accepted fact: the first boot of an image
built on a new day finds the stored magic stale and resets VIA's region.

Two rules bind whatever the tier:

- **When a change moves a mechanism an active document names, that text is
  updated in the same commit.** A document pointing at a moved mechanism is the
  stale-text failure this project keeps paying for, and it is the failure a
  refactor is most likely to introduce.
- **A tier pass is evidence about a binary, never a substitute for coverage.**

## Source Gate

- `quantum/matrix_common.c` is byte-identical to upstream, and `quantum/matrix.c`
  differs from the upstream base by **both** departures `era_invariants.md`
  admits — the two `MATRIX_SCAN_{RAW,COUNT}_DIAGNOSTICS_ENABLE` hooks
  with the time helpers and the include block they need, *and* the
  `debounce`/`matrix_post_scan` split — and by nothing else. Reading this line
  as naming only the split reports the hook family as an unrecorded fork edit,
  which is what it did until 2026-08-17.
  **The upstream base is the vendored QMK commit `c93ef27143`, by that name, on
  a development branch** — `git rev-list --max-parents=0 HEAD` returns six roots
  there, all of them QMK's own historical ones, so "this repository's first
  commit" does not resolve to a pristine tree and a diff against any of them is
  1200 files wide. On the shipped orphan it is that tree's single root instead;
  `era_qmk_fork_ledger.md` is canonical for both spellings. During an
  uncommitted implementation run, compare the working tree with
  `git diff c93ef27143 -- quantum/matrix.c`; after commit the `HEAD` form must
  agree. **Never compare against another ERA branch.**
- No dynamic allocation, Pico SDK queue, stock split serial backend, or
  synchronous core0 ERA wire fallback is linked.
- Replacement Apply has no alternate public EEPROM view. Before ADMIT every
  fallible identity/policy/schema/CRC/target/publication precondition is checked;
  after ADMIT the only persistence call is result-bearing
  `era_nvm_replace(... REMOTE_APPLY)`. The ERA NVM public RAM range remains old
  until durable commit and is new immediately afterwards. No State Sync or
  immutable success publication precedes that result, an NVM failure leaves the
  public image old, and a post-NVM publication failure repairs forward from
  canonical NVM rather than rewriting old EEPROM bytes.
- Every automated build emits exactly one unique resolved variant/tuple pair
  after make restarts are deduplicated. The requested and resolved names match;
  the resolved and compiled tuples match; the artifact stem and manifest use
  that resolved name. A non-split ELF admits only the all-`no` `standard`
  tuple.
- No `usb_disconnect`, `usb_connected_state`, or `usb_vbus_state` symbol is
  linked, and `is_keyboard_master_impl` resolves to the ERA override. This pair
  is the check that the override still does its job: if any of those symbols
  reappears, the stock boot-master poll has been relinked and the boot path has
  regained a blocking wait.
- No USB attach-gate or `startup_connect` symbol is linked.
- Core1 owns every active initiator/responder backend RX/TX operation,
  control-byte construction, sequence state, compact encode/decode, and result
  publication. Core0 owns authority/route policy, live QMK state, immutable
  semantic request/snapshot construction, and generation-matched apply.
- Core1 code has no live matrix, RGB, VIA, EEPROM, host LED, USB or HID read and
  no EEPROM write. Bare core1 and its PIO IRQ path call no ChibiOS thread
  suspend/resume or scheduler-lock API.
- No success response is sent before required result capacity is reserved and
  accepted source-push/chunk data is copied.
- **DUAL-HOST storage and runtime are not absence checks, and looking for absent
  symbols there proves nothing.** Both relations run the same engine and reuse
  the `0x20`/`0x21` envelope pair, so those ops are legitimately present in both
  and symbol absence cannot tell an opened *section* from an opened *relation*.
  What replaces it is a per-section eligibility check with three parts: exactly
  one send site and one admission site per direction AND against the linked
  eligibility table, with no other code opening a section; the ELF check below
  reading that table's bytes; and a `_Static_assert` beside it forbidding an
  entry that names a section outside the landed set. **Review the two AND sites,
  not a symbol list.**

## ELF And Static-Capacity Checks

Use targeted `arm-none-eabi-size`, `arm-none-eabi-nm` and
`arm-none-eabi-objdump`. There is no aggregate topology-script acceptance
surface.

**Three checks are enforced rather than inspected, and
`common/tools/era_residency_gate.sh <elf>` is where all three live**: the 32 KiB
free-ram0 floor, allocator absence, and the vector-table check. It takes an ELF
and nothing else and prints the three manifest fields. The internal launcher
calls it for every UF2 produced by `era-build`, so every copy-to-RAM board is
checked by the same code rather than by the same intention; there is no manual
per-board gate leg to remember before offering a build as evidence.

The vector check requires every `.vectors` entry except the reset slot to hold an
SRAM address, and the reset slot to still hold a flash one — boot2 fetches the
reset PC from flash, so that slot is the one legitimate exception and is checked
rather than skipped. **Reading the linked table's own bytes rather than a list of
handler names is the whole point**: the names `era_vector_defaults.c` overrides
are in SRAM by construction, so a check built from that enumeration cannot see
the failure that matters — a slot nobody installs falling back to the ChibiOS
weak default pinned in `.flash_startup`, which is silent. The linker `ASSERT` on
`era_unhandled_vector` is the complement, not a duplicate: it catches only the
whole object going missing. Both were injected and confirmed to fail, and the
byte check has since caught two real adoptions.

Every ERA split link emits a `.map` beside the ELF. Use it for the one question
`nm` and `objdump` answer badly — which linker stubs exist and why. Review the
`linker stubs` entries, not a `veneer` name grep: the two 16-byte stubs inside
`.flash_startup` serve the post-copy `__late_init`/`main` calls and are expected,
the 16-byte stub in `.sram_image` is the post-copy SRAM→flash call into the
pinned board object, and a **new** stub inside `.flash_startup` is the signature
of a pre-copy caller reaching into `.sram_image` — the failure that produces no
link error and no fault handler.

Inspect, when the change reaches them:

- `.text`/`.data`/`.bss` movement and linked stack reservations; wire formatter
  frames and nested core0 depth against `__process_stack_size__`;
- **a measured core1 stack figure, whenever a change adds core1 call depth.** The
  `_Static_assert` beside the reservation cannot report this and no compile-time
  construct can: neither `STACK_WORDS` nor `MIN_STACK_WORDS` is derived from the
  call graph. **Take it with `common/tools/era_core1_stack_walk.py <elf>`**,
  which is the committed instrument for this and nothing else: it walks the
  deepest frame sum reachable from `era_split_communication_core_entry()`
  (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`)
  along `bl` edges, reports recursion rather than following it, and adds the
  32-byte Cortex-M0+ exception frame, because core1 takes its deadline IRQ
  anywhere in that chain. Do not hand-walk it instead — a hand walk that
  under-reports reads as an improvement, and why the method is committed rather
  than re-derived per reading is `era_source_map.md`'s. **Compare the total
  against the linked reservation, not against the previous total: a delta is
  interesting but a headroom is the acceptance;**
- matrix, RGB, timer, backend and storage symbol placement; calls that escape an
  accepted RAM island into XIP; core1 storage undefined/call symbols for live
  EEPROM/QMK/VIA/RGB/matrix/USB/HID access;
- absence of stock serial, removed V16, core0 executor and retired stage
  symbols. `DUAL_RUNTIME_BUNDLE` is a **permanent** absence check — a stronger
  claim than "not yet", and one that stays cheap because no symbol of that name
  will be introduced;
- **the section eligibility matrix, read from the linked table's own bytes.**
  `arm-none-eabi-nm` for `g_era_split_wire_section_eligibility`, then
  `arm-none-eabi-objdump -s -j .sram_image` over its ten bytes: two per relation
  mode in `era_split_mode_planner.h` order, push direction then response.
  **The current expected reading is `00 00 / C5 FC / C5 FC / FE F7 / FE F7`** —
  no link, then the two HOST-PEER roles, then the two DUAL-HOST roles. One
  current value is carried, never a series. **Re-derive it from
  `era_split_wire_protocol.h`'s five `ELIGIBLE_*` masks in the same commit that
  opens a cell** rather than trusting this line — the line read `05 FC` and
  `3E F7` until 2026-08-17 because opening `STORAGE_PENDING` (`0x40`) in both
  push masks did not, which is the failure this instruction is against and not
  a hypothetical one. It moved again on 2026-08-18 when the restart arm
  (`0x80`) opened in the same two cells, and did **not** move on 2026-08-19 when
  that section was generalized from the link switch to any agreed act — the
  marker was renamed, no cell opened or closed, and re-deriving is what says so
  rather than assuming it — nor when, the same day, the link's boot convergence
  was rewritten to the listener-follows-talker rule, which touches no wire
  section at all. `era_wire_contract.md`'s eligibility columns stay the
  prose home of which relation may carry which section;
- the storage summary response's relation time-anchor seat still reads zero,
  validator-enforced, **permanently** — the anchor has one carrier in every
  relation;
- actual storage records and alignment against the aggregate static cap by
  equality (the cap's value is canonical in
  `era_host_peer_storage_contract.md`);
- automatic frame sizes, so no bulk/result record consumes core1 or process
  stack.

## Layout Checks

RAM placement rules, the load image scope and the budget are canonical in
`era_sram_residency_contract.md`. They apply to every ERA board, so these run per
board.

- Do not change layout while functional source is changing.
- Run `era_residency_gate.sh` on each keymap's ELF for the three enforced checks.
- Review with targeted objdump/nm: every ALLOC section VMA in ram0/ram4/ram5
  except boot2 and the flash LMA image, the vectors LMA at exactly `0x10000100`,
  the flash startup carve-out closed, no linked symbol outside that carve-out
  fetched from XIP after crt0, the load image's four redefined init/fini array
  symbols present, and the veneer map on a first link.
- **The double-tap bootloader magic is not accepted from an inferred
  placement.** It survives a reset because crt0's clear loop stops at
  `__bss_end__` and the word sits past it, and the ERA script claims
  `.ram0.bootloader_magic` into its own `.era_bootloader_magic` section
  immediately after `.bss` and asserts that section is exactly one word and
  starts at or after `__bss_end__`. Confirm in the linked ELF that
  `magic_location == __bss_end__ == __era_bootloader_magic_base__` and that
  `__era_bootloader_magic_end__ - __era_bootloader_magic_base__ == 4`.
  Disassemble `early_hardware_init_pre()` (`system/era_boot_core1_halt.c`) and
  require no call into `.sram_image`; the flash carve-out permits only the two
  established post-copy crt0 veneers.
- Repeat both-orientation storage, typing, RGB and recovery smoke on the exact
  selected load image, and establish a fresh no-cable qwin comparison point:
  scan rates are never compared across a layout change.

## Fixed Baselines

**These are the comparison points a later change is judged against, and this is
their one home.** Quote them from here and nowhere else. Each was taken with an
instrument this tree still carries, so a change that would move one re-takes the
figure rather than arguing about it.

- **Tomak RP2040 row-settle delay: 128 CPU cycles.** Lower values are rejected on
  device evidence, not caution: `32` and later `64` each produced false local
  matrix input while typing. Treat any lower value or board override as rejected
  until a separate electrical settle-margin test proves otherwise.
- **Split USART: 460800 baud.** `576000` and `748800` were tested and rejected —
  they produced source-push ACK gaps under high-rate typing — and stay rejected
  until separate signal-margin work proves otherwise.
- **Performance floor: 18 kHz on each half, cable or no cable** (owner decision
  2026-08-01). It catches "the keyboard became slow to use", cause unexamined.
  The lowest window ever recorded on the cabled rig is `21463`.
- **The PIO sampler's frame rate: `smp_hz=80129`** against a designed 80128 (a
  12.48 µs frame), with `ovr=0 rearm=0 fd1=0` on every window — the sampler's
  own health check, and any of them non-zero is a finding rather than a
  reading.
- **A render chunk costs `c + LEDs × e`**, with **c ≈ 8–11 µs a chunk** and
  **e ≈ 2.97 µs an LED** on a non-reactive effect. On the heaviest reactive
  effect `e` rises to **14.9 µs an LED** — **an effect changes `e` by a factor of
  five and leaves `c` alone**, which is why a chunk bound taken on one effect is
  not a bound. The fixed term is the per-chunk policy update, effect dispatch and
  `rgb_matrix_indicators_advanced()` call in `quantum/rgb_matrix/rgb_matrix.c`,
  so **it does not shrink when the chunk does**: the worst chunk `c + L·e` falls
  linearly in `L` while the frame's fixed cost `ceil(n/L)·c` rises as `1/L`.
  Between the shipping four and a hypothetical three the marginal price of a
  microsecond of worst case rises **six-fold**, which is the measured reason four
  is where the slicing stops.
- **The RGB frame rate: 62.34 fps with the idle gate, against 62.50 without it.**
  The 0.26 % is the gate sampling a millisecond clock at a period one pass longer
  than a millisecond, and it is the whole of the mechanism's behavioural residue.
  **It does not reach animation speed**: effect phase is computed from
  `g_rgb_timer`, the real millisecond clock, so what changes is how often a frame
  is started and not how fast anything moves.

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
option** (owner decision) — a product decision rather than a performance finding,
and what follows is that a HOST-PEER figure outranks a DUAL-HOST one wherever the
two disagree. Both cost gates pass on every row: the lowest window clears the
floor by a factor of 3.0, and every cabled ratio clears 95 % with at least 2.3
points to spare.

Three readings go with the table and each is a rule rather than a number:

- **The relation costs a HOST's core0 0.415 µs a pass**, 2.28 % of it; the
  DUAL-HOST pair reads 0.438 (initiator) and 0.497 (responder). **The responder
  pays more than the initiator**, which is the opposite of the intuition that the
  initiating half does the work, and it has been recorded twice.
- **A cabled figure reproduces about ten times worse than a no-cable one, and a
  power cycle is enough to show it** — 0.075 µs apart across a power cycle
  against 0.005 for two no-cable windows in the same sitting. A relation cost is
  a difference of two windows, one of which carries the cable, the peer's state
  and the host's own traffic. **Take a cabled figure twice, in two sittings,
  before believing a change of less than 0.1 µs in it.**
- **RIGHT runs 1.16 % below LEFT with no cable, and that is the halves' LED
  counts** — 55 against 41 is thirteen more render chunks and fourteen more LEDs
  a frame, which the chunk constants above predict at +0.18 µs a pass against
  +0.21 observed. It is not a side imbalance.

**The gate tightens as the pass gets faster, with no relation work at all.** The
relation costs 0.31–0.45 µs absolute, which is 1.5–2.2 % of an 18 µs pass and
would have been 0.7–1.0 % of the 44.9 µs pass this firmware started from. Any
figure quoted as "the relation costs X %" is meaningless without the pass it was
taken on.

**Two core1 figures any later core1 work is argued against**: the initiator's
core1 **sleeps 84.0 % of a cabled window**, the responder 89.1 %, both unchanged
to four digits across the last two comparison points — which is what says the
pass shortening is core0's alone. The PIO sampler's consumer costs **3.18 µs a
pass**.

**The core1 stack is 912 B used on `standard`, `qwin` and `qwin_phase`, and 992 B
on `wire`, `cause` and `stale`, against a 2048 B reservation**, all with the
exception frame included — so the acceptance is 1056 B of headroom in the worst
image, not one number.
**The figure is per variant and a reading that does not name its image is not a
reading**: the deepest chain in a diagnostics build is not the deepest chain in
the shipping one. Re-derive with
`python3 keyboards/era/common/tools/era_core1_stack_walk.py <elf>` on the ELF
beside the variant's UF2; the tool prints the chain and the headroom. What that
figure cannot see is an inlining change, so a change that adds core1 call depth
re-runs the walk rather than reasoning about it.

## The Split-Relation Cost Gates

**Two gates, side by side, and a candidate must pass both** (owner decision
2026-08-01). They exist because either alone admits a failure the other catches.

| Gate | Value | The failure it catches |
| --- | --- | --- |
| **Floor** | ≥ 18 kHz, cable or no cable | the keyboard became slow to use, cause unexamined |
| **Target** | cabled ÷ **same-image** no-cable ≥ **95 %** | the split relation is eating core0 |

**Compute the target per half and never as one figure for the pair.** The two
halves do not pay the same, and the current point records the responder paying
more. A pair-averaged ratio hides which half moved and would let a responder-side
regression spend the initiator's margin.

**The target is a ratio and must not be written as an absolute.** No-cable scan
rate reaches 20 kHz on this hardware, which makes 20 kHz the ceiling itself
rather than a target with headroom — so an absolute written at it would read
measurement noise as failure, and would silently pass a relation eating 2 kHz on
the day the ceiling becomes 22. The ratio is a same-image, same-board,
cable-only-different A/B, so it is single-variable and self-normalising. **And a
ratio alone cannot catch two numbers falling together**: a 14 kHz ceiling with
13.5 kHz cabled is 96 % and a slow keyboard. That is what the floor is beside it
for.

Two boundary conditions:

- **Nobody knows whether 5 % is a large margin or a small one.** What the
  relation costs is measured; whether 95 % is one step or an architecture away is
  not.
- **Below an 18.95 kHz ceiling the ratio stops binding**, because 95 % of it
  falls under the floor. At that point the ceiling has itself regressed, which is
  a different alarm and the floor still catches it — but do not cite "the ratio
  passed" as evidence there.

**The ownership rule that once kept RGB on core0 is withdrawn.**
What replaces it is an axis rather than a permission: **core0 owns the path from
a switch to a HID report, and core1 owns presentation.** The rule it replaces was
about *which kind of work* a core does; this one is about *what core0 must not
lose*, which is the thing these two gates actually measure. The withdrawal does
not license three things: it is not a claim that rendering is cheap to move (what
a source census established is only that the blockers were QMK's implementation
and not RGB's nature; the board indicator layer and core1's second deadline are
unchanged and are the whole of the remaining cost); it does not move the floor or
the target, both of which bind wherever the render runs; and it does not make the
ceiling a target. **Reopening the rule needs an owner decision, not a
measurement.**

## Evidence Storage

**Only a compact signature, and only where the fact it supports lives.** Raw
device logs and raw EEPROM bytes never enter a document; the rule itself is in
`AGENTS.md`. A measurement that settles a baseline replaces the old figure in
**Fixed Baselines** rather than joining it — a series of superseded figures is
how a reader comes to compare against the wrong one. A measurement that settles
nothing is not evidence and does not need a home.
