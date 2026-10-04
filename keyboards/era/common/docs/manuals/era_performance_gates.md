# ERA Performance Gates

Genre: manual
Canonical for: what a change owes before it is believed — the build, the source
and static checks, the refactor tiers, and every standing figure a change must
not regress

## Diagnostic Images And Measurement Authority

Build-variant definitions and diagnostic-axis defaults are source-owned by
`keyboards/era/era_build_options.mk`; selection rules are in
`era_build_options.md`. `standard` is the release image with the diagnostic
axes off. Diagnostic variants are measurement fixtures, not a second
implementation path, and are built through `era-build` only when their gate
calls for them, as described in `era_build_and_flash.md`.

The shipped TOMAK capture fixture is source-owned by
`keyboards/era/sirind/tomak/keymaps/via/keymap.c`; temporary bindings on another
board must be restored after measurement. Decode diagnostic output with
`era_capture_reading.md`.

**Do not infer a measured figure from source.** A constant is not a device
reading. **Fixed Baselines** below contain only rules or measurements that still
constrain later changes.

## What A Change Owes

Scoped by what the change touched. A change that touches no source owes none
of it — saying so is the verification statement.

| The change… | owes |
| --- | --- |
| touches any source | a build through `era-build keyboard:keymap`, whose first step synchronises the WSL local build tree to the change |
| claims to touch no behaviour | the **Refactor Self-Check** tier its binary earns |
| touches a closed surface, a retired path, or a QMK core matrix file | the **Source Gate** |
| touches RAM placement or the load image | the **Layout Checks**, per board |
| adds core1 call depth | a measured stack figure from `common/tools/era_core1_stack_walk.py` — no compile-time construct can report one |
| changes replacement Apply, the custom EEPROM adapter, ERA NVM, or dynamic-macro durability | `era_nvm`, `era_nvm_qmk_driver`, the directly affected storage/State-Sync/CLEAN tests under `tests/`, and a supported split build |
| restores or changes QMK wear-level files | all host-test targets owned by `quantum/wear_leveling/tests/` |
| changes scheduler request admission, queue freshness, or Core1 liveness | the scheduler admission/liveness device gate below at every supported link level |
| changes EEPROM CLEAN's reboot-durable prepare, agreed-restart phase machine, or storage quarantine | `era_split_restart_agreement`; a supported split build; and the EEPROM CLEAN agreement device gate below |
| `era_split_link_lifecycle` | production VIA/LINK/agreement, the scheduler's actual cold transition and real EEPROM/NVM with two independent endpoint RAM/NOR images: timeouts, stale intents, role/clock flips, physical-before-NVM ordering, serial/publication faults, program cuts, seal/activation readback ambiguity, reboot, reconciliation scoped to a meeting of the pair (a same-pair reopen after an agreement raises nothing; a fresh meeting, a winner change, an unagreed winner, or a recovery step off the agreed level does), and the pair-wide search report (the listener advertises `rate_searched` from its step until the relation it found opens or the episode ends; the settled initiator requests one LINK_RECOVERED rendezvous from its own search or that answer; an edge with neither local nor peer search, including a same-pair reopen without one, raises no report). The agreed presentation epoch is checked across both HOST orientations and DUAL-HOST, distinct serviced edges, late dispatch/display, timer wrap, role/clock changes, loss, duplication and hidden-report expiry. Physical owner/PIO/NOR and timing are simulated; `test_source_contracts.py` pins dirty-repair, storage admission and report dispatch wiring, not hardware timing |
| changes the PWM Backlight Pulse state machine or its speed table, the underglow Pulse engine in `features/era_rgblight_pulse.c` or the family policy in `features/era_pulse_policy.h`, the indicator-supply lock policy, the Tap Dance decision or synthesized-tap width rules in `features/era_tapdance.c`, or the non-split suspend bridge that retires Pulse/Breathing | `era_backlight_pulse`, `era_rgblight_pulse`, `era_rgblight_pulse_integration`, `python3 tests/era_rgblight_pulse/test_source_contracts.py`; a VIA build of one effect board and of one RGBLIGHT board; and, when the lock policy changes, a build of one lock-only board |
| changes the synthesized-tap width or the keyboard report interval | `era_hid_report_interval`, `python3 tests/era_hid_report_interval/test_source_contracts.py`, `era_backlight_pulse`; a build of one board; and the report interval device gate below |
| changes the physical-to-visual split wake | `python3 tests/era_split_visual_input/test_source_contracts.py`; a supported split build. The fixture executes the production scheduler entry and visual wake across roles, RGB policy and idle; wire latency and physical rendering remain device checks |
| changes common build-variant rules or the launcher identity | `tests/era_build_variant_rules/test_variant_rules.sh`, all six canonical variants on one representative split target, and Brick65 `standard`; each UF2 manifest must carry equal resolved/compiled tuples |
| would move a **Fixed Baseline** | an instrument, first |

The build itself — what WSL must provide, the automated command, and its
refusals as stop conditions — is `era_build_and_flash.md`'s.

### Focused host tests

Storage caller failure changes also run
`python3 -m unittest discover -s tests/era_persistence_failures -v`.
These fixtures compile production NVM/WL, adapters and callers, extract complete
boot/CLEAN functions, and require negative mutations to compile before their
runtime assertions fail. They model hardware; no device result follows from them.

The test and fixture source owns the target inventory and exact assertions.
ERA host tests live under `tests/`; QMK wear-level tests live under
`quantum/wear_leveling/tests/`. Run the target or source-contract script named
by **What A Change Owes** from the synchronized WSL-local tree. `make test:<target>`
is the QMK host-test entry; referenced Python or shell source-contract checks
run directly from their test directory.

Boot launch/admission, board runtime-setting initialization and the non-split
suspend deadline bridge run
`python3 -m unittest discover -s tests/era_boot_sequence -v`.
The fixture connects the production QMK/Bootmagic/matrix/scheduler/owner call
chain with simulated authority and hardware boundaries; its negative controls
must compile and fail at runtime. Physical RUN windows, first wire baud,
USB/HID readiness and first LED output still need device evidence.

Build-variant identity is the exception worth spelling out:
`tests/era_build_variant_rules/test_variant_rules.sh` checks the make-layer
selection rules, while `common/tools/era_qmk_build.sh` binds the resolved tuple
to the linked ELF and manifest. A make-only variant pass is not launcher
evidence.

A Tap Dance ownership or Caps feedback change also runs `python3` on
`tests/era_backlight_pulse/td_ownership_selftest.py` and
`tests/era_backlight_pulse/caps_feedback_selftest.py` after `era-sync`. Each
control recompiles one isolated source mutation with the native build's flags
against unchanged objects and counts only when it compiles and its named
runtime assertion then fails. They prove host behaviour, not GPIO timing, board
geometry or an `era-build` gate.

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

### LINK SPEED lifecycle device gate

After both host suites and supported builds, exercise High/Medium/Low in both
DUAL-HOST directions and with either physical half as HOST. Hold/release keys
through Apply without a USB re-enumeration; inspect unchanged USB identity and
continued reports. Test cable removal, role flip, delayed Core0 and reboot at
request/arm/transition/write boundaries, then reconnect. Verify actual baud
transition skew, Core1 READY/cap counters, liveness recovery, durable state
on both halves and explicit retry after a fault. Repeat during ordinary VIA
and storage activity. The shared wire agreement has no bilateral durable
completion acknowledgement; do not infer that guarantee from local success.

For LINK_RECOVERED, provoke a listener rate search and a successful reopened
relation in DUAL-HOST and with either half as HOST at High/Medium/Low. On the
same manifest-selected standard image on both halves, measure both green
pulses' on/off edges against one capture clock. Compare with the pre-change
image under the same load; record residual edge skew, not just visual judgment.
Repeat with ordinary VIA/storage activity and verify that launch/failure/sleep
priority can mask, but never postpone or replay, the agreed report. Host tests
prove a shared software epoch, not a physical LED latency bound. They also
exercise notification preparation during the rate-confirmation window, no
raw-HID quiet wait for presentation, and cancellation after a failed
confirmation but before the accepted report deadline.
Also test the already-matching running level after listener search: a Left
HOST / Right PEER handoff must produce one pair report even when no idle
publication crosses between rate completion and the report. Repeat all three
found and stored target levels, both HOST orientations and DUAL-HOST; sampled
latest-state tests must not inject an intermediate idle to make this pass.

For discovery, compare the same cable, rates and attach sequence before and
after: measure first wire activity, successful SESSION_STATUS and the green
edges separately. Check all initial/target levels, both HOST orientations,
DUAL-HOST, cold/late attachment, noise and loss. Silence and a single boot
break must not change rate; any valid frame in the complete listener window
must veto a step. Verify no persistence from search. Measure the deliberate
no-cable probe increase against the configured completion-to-next-probe
interval, including actual TX/wait time and the initial ten-miss bootstrap.
There must be no independent Right send and no steady-relation cadence change.
Compare Core0 scan rate and Core1 parked time on like-for-like diagnostic
images with the long-park caveat in `era_capture_reading.md`'s **The qwin Window**.
Host-injected arrivals prove policy, not CPU percentages or electrical margins.
`tests/era_split_link_lifecycle/test_discovery_cadence.py` compiles the actual
sender period, due, reset and completion functions against controlled time.

For the explicit local LINK Apply receipt, check a changed level, the same
runtime-and-saved level, a busy agreement, cancellation, and an injected local
runtime/NVM failure. Only the commanded half reports the result: no pending
frame, one green pulse after checked runtime plus persistence (or no work),
red otherwise. A local success must not wait for raw-HID quiet or the full
liveness-confirmation window. A failed write must never produce green even
when runtime already changed. A later failed health check must correct the
receipt without resetting; successful observation must not replay green.
Measure click-to-transition, the NVM call, and transition-to-first-green
separately under identical load. Host-test injected delays are not flash timings.
Compare the read-only local result, configured runtime and confirmed saved
labels from the matching VIA definition; the consumed Apply toggle is not a
receipt. UI refresh, colour visibility and held-key continuity are device/app
checks, not claims made by the host fixture. No case should reset or
re-enumerate USB merely to acknowledge Apply.

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
| unrelated save during macro staging | rotation with an open upload: elapsed time including committed-macro replay, unrelated setting readback, old complete macro after interruption, new complete macro only after CLOSE; MACRO revision follows public bytes/marker changes without granting early completion |
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

The host-regression legs `era_split_restart_agreement` already covers. A
device run must not manufacture them.

### Report interval device gate

Entry: any ERA board on a host capture (USBPcap or a USB analyzer), NKRO
off and then on; a key mapped to `LT(1, KC_CAPS)` and a Tap Dance slot with
On Tap `KC_CAPS`, On Hold `MO(1)`.

For a short tap on each key the capture must show the Caps-down and Caps-up
reports at least `TAP_HOLD_CAPS_DELAY` apart measured from the down report's
completion, a letter typed inside that window delivered after the Caps-up
report, and a mouse move inside the window delivered without delay. The
scan-rate figures under **Fixed Baselines** must not move: the loop waits for
nothing. On macOS the tap must toggle Caps every time, five taps in a row.

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

**T1's identity does not depend on the build day.** `QMK_BUILDDATE`, the
source of the VIA EEPROM magic, is pinned in `storage/era_via_magic.h`, so
the same source rebuilt on another day produces the same `uf2_sha256`. A
mismatch is a real difference to explain.

- **When a change moves a mechanism an active document names, that text is
  updated in the same commit.**
- **A tier pass is evidence about a binary, never a substitute for coverage.**

## Source Gate

| Check | Accept |
| --- | --- |
| matrix files | `quantum/matrix_common.c` byte-identical to upstream. `quantum/matrix.c` differs by both departures `era_invariants.md` admits — the two `MATRIX_SCAN_{RAW,COUNT}_DIAGNOSTICS_ENABLE` hooks with the time helpers and include block they need, *and* the `debounce`/`matrix_post_scan` split — and by nothing else. Uncommitted: `git diff c93ef27143 -- quantum/matrix.c`; after commit the `HEAD` form must agree. **Never compare against another ERA branch.** Upstream-base spelling: `era_qmk_fork_ledger.md` |
| absence | no dynamic allocation, Pico SDK queue, stock split serial backend, or synchronous core0 ERA wire fallback is linked. Allocator entry points are the residency gate's; the rest are review |
| replacement Apply | no alternate public EEPROM view; ADMIT / old-or-new / repair-forward order is `era_host_peer_storage_contract.md` **Replacement Apply: ADMIT And Public Authority** |
| variant identity | every automated build emits exactly one unique resolved variant/tuple pair after make restarts are deduplicated; requested and resolved names match; resolved and compiled tuples match; artifact stem and manifest use that resolved name; a non-split ELF admits only the all-`no` `standard` tuple |
| USB master pair | no `usb_disconnect`, `usb_connected_state`, or `usb_vbus_state` symbol is linked, and `is_keyboard_master_impl` resolves to the ERA override. If any of those symbols reappears, the stock boot-master poll has been relinked |
| attach | no USB attach-gate or `startup_connect` symbol is linked |
| core ownership | Core1 / Core0 ownership of backends, policy, live QMK state and apply is `era_authority_contract.md` / `era_invariants.md`. Core1 has no live matrix, RGB, VIA, EEPROM, host LED, USB or HID read and no EEPROM write. Bare core1 and its PIO IRQ path call no ChibiOS thread suspend/resume or scheduler-lock API |
| capacity before success | no success response that introduces new Core0 work is sent before required result capacity is reserved and accepted source-push/chunk data is copied. An exact duplicate successful HEARTBEAT may reuse an already-pending result only under that invariant; SESSION/push or a changed snapshot/mask cannot |
| DUAL-HOST eligibility | not an absence check — both relations run the same engine and reuse the `0x20`/`0x21` envelope pair. Three parts: exactly one send site and one admission site per direction AND against the linked eligibility table, with no other code opening a section; the ELF check below reading that table's bytes; a `_Static_assert` beside it forbidding an entry that names a section outside the landed set. **Review the two AND sites, not a symbol list.** Which relation may carry which section: `era_wire_contract.md` **Section eligibility** |

## ELF And Static-Capacity Checks

Targeted `arm-none-eabi-size` / `nm` / `objdump`. No aggregate
topology-script acceptance surface.

**Three checks are enforced rather than inspected, in
`common/tools/era_residency_gate.sh <elf>`**, and only on a UF2 (RP2040)
image. `common/tools/era_qmk_build.sh` calls it after every UF2 link; Brick65
hex records `residency_gate=not applicable`. ELF in, three manifest fields
out:

| Field | Accept |
| --- | --- |
| `ram0_resident_bytes` / `ram0_free_bytes` | sum of ALLOC ram0 sections excluding `.heap`; report unused capacity. Every allocation must fit inside its physical SRAM bank outside the ROM scratch reservation or the flash startup carve-out, with no overlaps (including `.heap`). An empty or malformed section walk fails |
| allocator | `malloc`, `calloc`, `realloc`, `_sbrk`, `chCoreAlloc`, `chCoreAllocI`, `chCoreAllocFromBaseI`, `chCoreAllocFromTopI`, `chHeapAlloc` undefined |
| `vectors_gate` | `.vectors` holds 48 entries; index 1 (reset) is flash (`0x10000000`..`0x1fffffff`); every other entry is SRAM (`0x20000000`..`0x20041fff`). Read the linked table's own bytes, not handler names |

The linker `ASSERT` on `era_unhandled_vector` is the complement: it catches
only the whole object going missing. Canonical here per `era_invariants.md`.
Capacity semantics: `era_sram_residency_contract.md` **Verification**. Main-SRAM
free space cannot substitute for either core's separately reserved stack
headroom. The gate's parser/boundary fixtures and real ARM ELF positive/negative
checks run with `python3 tests/era_residency_gate/test_residency_gate.py` in the
configured WSL toolchain; rerun them when changing the gate.

Every ERA split link emits a `.map`. Review `linker stubs`, not a `veneer`
name grep: two expected 16-byte stubs in `.flash_startup` serve the
post-copy `__late_init`/`main` calls; one 16-byte stub in `.sram_image` is
the post-copy SRAM→flash call into the pinned board object; a **new** stub
inside `.flash_startup` is a pre-copy caller reaching into `.sram_image`.

| When the change reaches… | Instrument |
| --- | --- |
| `.text`/`.data`/`.bss` movement; linked stack reservations; wire formatter frames and nested core0 depth against `__process_stack_size__` | `size` / `nm` / `objdump` |
| added core1 call depth | `common/tools/era_core1_stack_walk.py <elf>` walks the deepest frame sum reachable from `era_split_communication_core_entry()` (`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`) along `bl` edges, reports recursion rather than following it, and adds the 32-byte Cortex-M0+ exception frame. Do not hand-walk. Compare the total against the linked `g_era_split_communication_core_stack` size; headroom is the acceptance, a delta is not |
| matrix, RGB, timer, backend and storage symbol placement; calls that escape an accepted RAM island into XIP; core1 storage undefined/call symbols for live EEPROM/QMK/VIA/RGB/matrix/USB/HID access | `nm` / `objdump` |
| stock serial, removed V16, core0 executor and retired stage symbols | absence. `DUAL_RUNTIME_BUNDLE` is a **permanent** absence check |
| section eligibility matrix | `arm-none-eabi-nm` for `g_era_split_wire_section_eligibility`, then `arm-none-eabi-objdump -s -j .sram_image` over its ten bytes: two per relation mode in `era_split_mode_planner.h` order, push then response. Expected reading, also pinned by `era_split_restart_agreement`: `00 00 / C5 FC / C5 FC / FE F7 / FE F7`. One current value, never a series. Re-derive from `era_split_wire_protocol.h`'s `ELIGIBLE_*` masks in the same commit that opens a cell |
| storage summary response time-anchor seat | still reads zero, validator-enforced, **permanently** — the anchor has one carrier in every relation |
| actual storage records and alignment | against the aggregate static cap by equality; the cap's value is `era_host_peer_storage_contract.md` |
| automatic frame sizes | no bulk/result record consumes core1 or process stack |

## Layout Checks

RAM placement, load-image scope and budget:
`era_sram_residency_contract.md`. Per board. The live instrument is
`common/tools/era_residency_gate.sh` (SRAM budget, allocator, `.vectors`).
The contract's **Verification** section points here; this procedure is
**Layout Checks**.

| Check | Accept |
| --- | --- |
| mixing | do not change layout while functional source is changing |
| enforced gate | `era_residency_gate.sh` on each RP2040 keymap's ELF for the three enforced checks |
| objdump/nm | every ALLOC section VMA in ram0/ram4/ram5 except boot2 and the flash LMA image; vectors LMA at exactly `0x10000100`; flash startup carve-out closed; no linked symbol outside that carve-out fetched from XIP after crt0; the load image's four redefined init/fini array symbols present; veneer map on a first link |
| double-tap magic | not accepted from an inferred placement. Confirm `magic_location == __bss_end__ == __era_bootloader_magic_base__` and `__era_bootloader_magic_end__ - __era_bootloader_magic_base__ == 4`. Why the word survives a reset is the residency contract's. Disassemble `early_hardware_init_pre()` (`system/era_boot_core1_halt.c`) and require no call into `.sram_image`; the flash carve-out permits only the two established post-copy crt0 veneers |
| smoke | both-orientation storage, typing, RGB and recovery smoke on the exact selected load image, plus a fresh no-cable qwin comparison point. Scan rates are never compared across a layout change |

## Fixed Baselines

**These are the comparison points a later change is judged against, and this
is their one home.** Quote them from here and nowhere else. A `#define` is
listed only when it is still the rule a later change must not move without
its own gate.

| Baseline | Value | Device grounds | Instrument |
| --- | --- | --- | --- |
| ERA RP2040 row-settle | family default `ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY` 256 cycles; release `ERA_RP2040_MATRIX_PIO_ROW_RELEASE_CYCLES` 64 cycles. `era_rp2040_matrix_pio` encodes that pair as 323 slot cycles | Owner-selected H1 A policy: the previously failing H1 operated normally with this pair. This is evidence for that device, not a measured margin for every board. Earlier TOMAK tests rejected settle `32` and `64` for false local input. Lowering the family default or overriding a board requires electrical settle-margin evidence | matrix / typing; PIO host test |
| Split USART | `ERA_SPLIT_SERIAL_USART_SPEED` 460800 baud. `split/era_split_via_link.c` `#error`s any other High | `576000` and `748800` produced source-push ACK gaps under high-rate typing; stay rejected until separate signal-margin work proves otherwise | wire / typing |
| Performance floor | 18 kHz on each half, cable or no cable | This standing usability floor moves only with its own controlled real-device gate | qwin |
| Core0 process stack | `USE_PROCESS_STACKSIZE` 0xA00 (2560 B), set in `features/era_tapdance_rules.mk`; SRAM4 keeps 224 B free. Deepest core0 chain on TOMAK `via standard`: a Tap Dance action running `QK_BOOT` whose shutdown flushes pending config to ERA NVM, about 2096 B with the exception frame, above QMK's 2048 B default | Static reading only; no device ground | `common/tools/era_core1_stack_walk.py <elf> <root>` per segment, summed across the dance callback (an indirect call the walker does not follow). `eeprom_update_block`'s variable-length buffer is not modelled, so the true figure is higher |
| PIO sampler frame rate | designed about 48375 Hz (20.672 µs frame = 8 slots × 323 cycles at 125 MHz); this is sampler arithmetic, not a measured Core0 scan rate | `ovr=0 rearm=0 fd1=0` on every window; any of them non-zero is a finding | qwin / PIO; `era_rp2040_matrix_pio` |
| Render chunk | `c + LEDs × e` with `led_process_limit` 4 on TOMAK79H (`keyboard.json`); **c ≈ 8–11 µs** a chunk; **e ≈ 2.97 µs** an LED on a non-reactive effect; **e ≈ 14.9 µs** on the heaviest reactive effect (five-fold in `e`, `c` unchanged) | the fixed term is the per-chunk policy update, effect dispatch and `rgb_matrix_indicators_advanced()` call in `quantum/rgb_matrix/rgb_matrix.c`, so it does not shrink when the chunk does. Worst chunk `c + L·e` falls linearly in `L` while the frame's fixed cost `ceil(n/L)·c` rises as `1/L`. Between the shipping four and a hypothetical three the marginal price of a microsecond of worst case rises **six-fold** | RGB / chunk |
| RGB frame rate | 62.34 fps with the idle gate, against 62.50 without it | the 0.26 % is the gate sampling a millisecond clock at a period one pass longer than a millisecond, and is the whole of the mechanism's behavioural residue. **It does not reach animation speed**: effect phase is computed from `g_rgb_timer` | RGB |

Changing any fixed baseline requires its own controlled real-device gate.


## The Split-Relation Cost Gates

**Two gates, side by side, and a candidate must pass both.** Either alone
admits a failure the other catches.

| Gate | Value | The failure it catches |
| --- | --- | --- |
| **Floor** | ≥ 18 kHz, cable or no cable | the keyboard became slow to use, cause unexamined |
| **Target** | cabled ÷ **same-image** no-cable ≥ **95 %** | the split relation is eating core0 |

**Compute the target per half, never as one figure for the pair.** **The
target is a ratio and must not be written as an absolute.**

> **REFUSED:** an absolute kHz target in place of the 95 % ratio.
> **WHY:** no-cable scan rate sits near 20 kHz here, so a ceiling absolute
> reads window noise as failure; a ratio alone cannot catch two numbers
> falling together (13.5 / 14 kHz is 96 % and a slow keyboard).
> **REOPENS:** a different comparison point that is not a same-image ratio.

- **Nobody knows whether 5 % is a large margin or a small one.** (owner)
- **Below an 18.95 kHz ceiling the ratio stops binding**, because 95 % of it
  falls under the floor. Do not cite "the ratio passed" as evidence there.

> **REFUSED:** quote "the relation costs X %" without the pass it was taken on.
> **WHY:** the same absolute relation cost is a different share of an 18 µs
> pass than of a slower one, so the percentage is not a portable figure.
> **REOPENS:** a comparison that names the pass.

**Core0 owns the path from a switch to a HID report; core1 owns presentation.**
This does not claim rendering is cheap to move, move the floor or target, or
make the ceiling a target. **Reopening the rule needs an owner decision, not a
measurement.**

## Evidence Storage

A new measurement replaces the figure in **Fixed Baselines**, never a
series. Raw-log rule: `AGENTS.md`.
