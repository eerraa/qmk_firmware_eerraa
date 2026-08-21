# ERA SRAM-Resident Load Image Contract

Status: active; in force.
Genre: contract
Canonical for: ERA RP2040 memory placement and the load layout, copy-to-RAM
execution scope, the flash startup carve-out, the SRAM budget structure and
growth reserves, the boot-ordering invariant, the durable-apply liveness
design, and the closed placement surfaces
Read when: changing the load layout, the linker script, the startup carve-out
or the core1 halt inside it, memory placement or core1 launch resources,
scan/transaction scheduling assumptions, the durable-apply liveness path, or
migrating a board onto this image

## Why The Image Is Resident

- RP2040 physical constraint: the single external QSPI NOR flash cannot serve
  instruction fetches while a program/erase is in progress, and the SSI
  controller leaves memory-mapped XIP mode to issue command sequences. Any
  core executing from flash must therefore stop for the duration of a flash
  commit. Every rule below exists because of that sentence.
- Recorded growth reserves, in consumption order: the production release
  profile (about 24 KiB more headroom than wire, which carries the diagnostics
  worst case), the 16 KiB XIP cache reclaim, and LTO (off; expected 5-15%
  `.text` reduction, and it requires its own measured gate before adoption).
  A per-board or per-profile free-RAM figure is a build's own output rather
  than a recorded one — `common/tools/era_residency_gate.sh` prints it from the
  ELF, and no document carries a table of them to go stale. The standing
  requirement is the 32 KiB floor under **Verification** below.

## Load Layout

- The whole firmware image executes from SRAM on both cores (copy-to-RAM).
  Flash keeps only boot2, the vectors LMA, the flash-resident startup
  carve-out, the `.sram_image` LMA, and the wear-leveling backing store.
- Main SRAM (`ram0`, 256 KiB striped) holds the vector table, the copied
  image, `.bss`, and the elastic heap remainder, with at least 32 KiB free on
  every profile.
- Scratch banks are per-core private, so core1 stack and vector fetches never
  contend with core0 traffic in the striped main SRAM. `ram4` (SRAM4,
  `0x20040000`, 4 KiB) holds the core0 main/process stacks (3 KB) plus 288 B
  of ChibiOS core0 structures. `ram5` (SRAM5, `0x20041000`, 4 KiB) holds the
  ERA core1 stack and its 48-entry vector table through `.ram5_clear` input
  sections — 2240 B together — leaving about 1.6 KiB unclaimed below the
  `ram7` boot carve-out (`0x20041f00`, 256 B) that the ROM scribbles during
  reset.

## Ownership Does Not Move With Placement

- The matrix engine owns raw scan, debounce runtime, local/composed rows,
  HOST-PEER source/cache/projection state, and changed-state publication.
- QMK core0 owns key processing, RGB rendering, VIA/configuration, EEPROM,
  USB, and HID behavior.
- The scheduler owns relation/route policy and immutable publication.
- The transaction engine owns compact serial execution above the ERA RP2040
  backend; core1 is the only active backend executor and consumes only
  immutable semantic records and bounded copied data.
- **SRAM residency grants no new access**: core1 still has no live
  QMK/RGB/VIA/EEPROM/USB/HID access and no EEPROM write.

## Scheduling Discipline

Placement relaxes none of these:

- Storage capture, EEPROM read/write, CRC, comparison, route enqueue, result
  drain, apply slices, runtime reload, and diagnostics execute only at
  task/housekeeping boundaries, never inside matrix scan.
- Matrix-scan transport reads cached scalar facts only.
- Diagnostics formatting and snapshot construction stay outside matrix-scan
  hooks and are explicitly paced.
- The transaction backend boundary: role lifecycle, send, response-window RX,
  and responder-idle RX execute only under the core1 owner epoch, and IRQ code
  does not drain or validate frames. Which file owns which of those primitives
  is canonical in `era_source_map.md`.

## Load Image Scope

- The layout is the common-layer linker script
  `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`, selected with `MCU_LDSCRIPT`
  in `system/era_sram_resident_rules.mk`, which every RP2040 board under
  `keyboards/era` includes from its `post_rules.mk`. One finished layout, no
  legacy XIP path, and no QMK-common, ChibiOS, or pico-sdk file changes.
  RP2040-only.
- Flash keeps only: `.boot2` at VMA==LMA `0x10000000` (the wear-leveling
  driver reads the linked `BOOT2_ROM` symbol through XIP), the `.vectors` LMA
  at exactly `0x10000100` (the prebuilt boot2 blob hardcodes that address for
  the reset MSP/PC fetch), the flash-kept startup carve-out, and the
  `.sram_image` LMA load image.
- The carve-out pins every pre-copy-reachable object to flash VMAs:
  `crt0_v6m.o(.text)`; `vectors.o(.text)`, placed adjacent to crt0 because the
  `b _crt0_entry` short branch reaches only ±2 KB; `crt1.o`; the ChibiOS board
  object, matched **path-anchored** — never a bare `*board.o` suffix pattern,
  which matches every class skeleton and every `*_keyboard.c` in the build as
  well as the one intended object. In this split build that is **five**
  (`quantum/keyboard.o`, `src/default_keyboard.o`,
  `split/era_split_keyboard.o`, `split/era_split_board.o`, and the intended
  `board.o`); a non-split build substitutes `system/era_nonsplit_board.o` for
  the skeleton and has its own five; the QMK `chibios.o` early-init trio
  (`__early_init`, `early_hardware_init_pre/post` — QMK renames the board's
  `__early_init` away, so the strong symbol lives in
  `tmk_core/protocol/chibios`); and
  `system/era_boot_core1_halt.o`.
- That last object's strong `early_hardware_init_pre()` owns the whole
  pre-copy window: it hardware-halts core1 before the copy loops run over the
  image core1 is executing, and it arms the double-tap bootloader window and
  counts the reset as a tap (both invariants in `era_invariants.md`). Two
  unrelated jobs in one function because crt0 offers one call on that side of
  the copy loops; a second pinned TU would add a carve-out selector, an
  `ASSERT` pair, and a cross-unit call to the one place where a
  stopped-matching selector produces a silent veneer.
- Board-extensibility precondition: the carve-out selectors name the default
  ChibiOS board object path (`RP_PICO_RP2040/board.o`), so every ERA board must
  keep the default `BOARD`, or amend the carve-out in the same change. A
  violation lands `__chibios_override___early_init` in SRAM and bricks the
  pre-copy path; that specific violation is caught at link time by the
  `ASSERT`s below. **Closure itself is not caught.** The build-time check is on
  symbol placement, so closure remains the deliberately manual objdump/nm
  review defined in `era_performance_gates.md`, not an automated script.
- The carve-out has six pinned TUs plus two linker-generated veneers
  (`__main_veneer`, `____late_init_veneer`) serving the legitimate post-copy
  calls at `crt0_v6m.S:289`/`:306`. **The veneer set, not a byte count, is the
  gate: a third veneer is the pre-copy-caller signature.** Boot2, the vectors
  LMA and the carve-out are byte-contiguous, so growth slides the
  `.sram_image` LMA upward by exactly that growth; flash is not the
  constraint. Pinning an existing TU costs **negative** SRAM because its
  `.text`/`.rodata` leaves `.sram_image`; pinning a flash-only TU such as the
  core1 halt costs zero.
- The `ASSERT`s: `__flash_startup_base__`/`__flash_startup_end__` bracket the
  section and every function named in the pre-copy call closure must link
  inside them, with `early_hardware_init_pre` held to the tighter
  `__era_boot_core1_halt_base__`/`_end__` pair so a dropped `SRC` entry cannot
  leave QMK's empty weak stub satisfying the loose bound while the halt
  silently does nothing. They are a necessary condition, not a closure proof —
  a pinned function calling an unpinned one still passes — and they were
  validated by injecting each of the three ways to break the placement: a
  removed `chibios.o` selector, a removed `SRC` line, and a mismatched
  carve-out selector.
- The three non-obvious rules that bind any addition to the carve-out — the
  silent flash→SRAM veneer, the mandatory `.rodata` arm, and the
  selector-ordering constraint on the `Reset_Handler`→`_crt0_entry` short
  branch — are written at the selector list in the linker script rather than
  here, because that is the file an editor is looking at when they apply.
- Everything else lands in one `.sram_image` output section with ram0 VMA and
  flash LMA, copied whole by the stock crt0 `CRT0_INIT_DATA` loop via redefined
  `__textdata_base__/__data_base__/__data_end__`; `.vectors` copies through the
  stock `CRT0_INIT_VECTORS` loop, which then retargets VTOR to the SRAM table.
  The section KEEPs init/fini arrays and redefines their four symbols, carries
  glue/extab/exidx inputs, and ends `ALIGN(4)` for the word copy loop. Stock
  `rules_stacks.ld` and `rules_memory.ld` stay INCLUDEd (their `-L` directories
  survive the script move; the QMK timecrit data rule's does not, so its
  content is inlined instead). `rules_stacks_c1.ld` is deliberately NOT
  included — the c1 stack reservation is dropped, consistent with
  `CRT0_EXTRA_CORES_NUMBER=0`, because ERA launches core1 itself rather than
  through the ChibiOS second-core path.
- Accepted residual: `CRT0_VTOR_INIT=1` points VTOR at the not-yet-copied
  SRAM table for the few hundred pre-copy cycles; only an unmaskable
  fault in that window would misvector, and a pre-copy fault is fatal
  either way. Accepted to keep the vendor makefiles untouched.

## Vector Table Residency

- Every `.vectors` entry except index 1 holds an SRAM address. Index 1 is
  `Reset_Handler` and stays in flash by design: boot2 loads MSP/PC from the
  vectors LMA at `0x10000100`, so the reset PC has to be fetchable before any
  copy loop has run.
- Without a strong definition an entry holds the collapsed ChibiOS weak
  default inside `vectors.o(.text)`, which the carve-out pins to flash for
  `Reset_Handler`'s sake, so every fault and every uninstalled IRQ would vector
  into flash. The carve-out itself is the cause of that, not XIP inheritance.
  `system/era_vector_defaults.c` supplies strong SRAM definitions for those
  symbols, aliased onto one `era_unhandled_vector`. It writes 38
  `ERA_VECTOR_DEFAULT` entries — 33 unconditional and five that three `#if`
  groups select from the board's PWM, PIO and DMA driver set
  (`era_board_adoption.md`'s **Copy-To-RAM Policy**), so how many a given image
  links is a board fact and not a constant. Re-derive with
  `grep -c '^ERA_VECTOR_DEFAULT(' keyboards/era/common/system/era_vector_defaults.c`. Every ChibiOS default
  is `.weak`, so this costs no ChibiOS or QMK edit and touches no carve-out
  selector: `vectors.o` stays pinned and only the linker's resolution of the
  table entries moves. It costs 16 bytes of ram0.
- Overriding `_unhandled_exception` alone does not work, and it is the obvious
  wrong shortcut. `vectors.S` places dozens of `VectorNNN:` labels
  consecutively with no instruction between them, so they collapse onto one
  address that falls through into a single `bl _unhandled_exception`. A strong
  `_unhandled_exception` moves only the branch target; every table entry this
  file defines goes on pointing at the flash `bl`.
- Two checks hold this and they are complements, not duplicates. The `.vectors`
  gate in `era_performance_gates.md` reads the linked table's own bytes, which
  is the only thing that can catch a slot nobody installs falling back to the
  flash default. The linker `ASSERT` on `era_unhandled_vector` catches the
  whole object going missing, and is what the boards that do not run the
  tomak79h launcher get.
- Landing on an uninstalled vector is still a hang, exactly as the ChibiOS
  default was. What this buys is attribution, not recovery, and no fault
  recovery exists or is planned.

## Double-Tap Magic Placement

- The double-tap bootloader magic is one word that must survive a reset, so it
  has to sit outside every crt0 copy and clear range. The script claims it by
  name: `.era_bootloader_magic (NOLOAD)` immediately after `.bss` `KEEP`s the
  `.ram0.bootloader_magic` input that `magic_location`
  (`platforms/chibios/bootloaders/rp2040.c`) is tagged with, and two `ASSERT`s
  hold it to exactly one word starting at or past `__bss_end__`.
- Placement is order-sensitive in two directions. Immediately after `.bss`
  because that is where crt0's clear loop stops, and before
  `INCLUDE rules_memory.ld` because `ld` assigns an input section to the first
  output section that matches it — the ChibiOS `*(.ram0.*)` sweep only loses
  the word if the claim is read first.
  Because the arm runs ahead of the copy loops, a misplaced word has crt0's
  `.bss` clear erase an arm written moments earlier in the same boot — a
  failure indistinguishable from the dead zone the pre-copy arm exists to
  remove.
- `magic_location` is deliberately global because the pinned pre-copy
  translation unit writes it. The linker asserts both that its region is one
  word outside the clear range and that the symbol equals
  `__era_bootloader_magic_base__`; the Layout Gate independently compares the
  linked addresses. A changed section attribute empties the region and fails
  the size `ASSERT`, while a disconnected writer fails the symbol/base
  `ASSERT`. There is no surviving source-only placement question.
- **The dead zone the pre-copy arm removed was never measured, and cannot be.**
  This is recorded so nobody spends the effort again. RP2040 has no timebase
  counting from reset release before `clocks_init()` runs, so the width cannot
  be read from inside the firmware. Timing it by hand is below human resolution
  — 0.2 s and 0.4 s are neither producible nor distinguishable. And a
  firmware-issued reset is not the same event: `NVIC_SystemReset()` asserts
  SYSRESETREQ, which on RP2040 resets only the requesting core
  (`era_invariants.md`), so CLOCKS survives and crt0 re-runs at the configured
  system clock rather than the reset ROSC. The useful corollary is that the
  dead zone is a physical-reset property only, which bounds any test of this
  area to RUN-pin and cold boots. The 35-80 ms figure that circulated during
  the work was arithmetic from the copy size and the ROSC rate; it is not a
  measurement and must not be repeated as one.

### Pre-copy reset guard layout

- The production no-init exception remains exactly the four-byte
  `magic_location` region. There is no second retained trace or selector-only
  carve-out; adding any other pre-copy SRAM object reopens the invariant.
- `era_boot_core1_halt.c` is flash-pinned and may touch only reset-domain MMIO,
  ROSC `COUNT`, the retained magic, and stack locals before crt0 copies SRAM.
  Its stable-release counter and every poll loop are bounded, failure leaves
  CLEAR, and the linked function may contain no call into `.sram_image`.
- The fixed successful guard is 768 chunks of 240 ROSC ticks. It may write no
  oscillator configuration, divider, frequency, XOSC, or clock-tree register.
- Watchdog scratch0 is reset-domain MMIO, not a no-init allocation. The hook
  reads it as part of exact physical-RUN classification and writes the
  survival marker before later software reset routes can run.
- The post-copy timeout state lives in ordinary `.bss`. Its started bit is
  separate from the timestamp because zero is a valid first timer sample;
  disarm clears both fields and the retained magic.

## Core1 Vector Path

The ERA launcher copies `SCB->VTOR` into its 48-entry, 256-byte-aligned
SRAM table at first launch and patches TIMER_IRQ3; restarts never re-send
the vector address. With core0 VTOR already at the SRAM vectors after
crt0, both-core SRAM VTOR holds by construction.

## Flash Write Guards And Boot Ordering

- `eeprom_driver_write_begin_kb`/`end_kb` call no core1 stop/reset helper.
  They are commit-window recorders: `fwg` counts commit windows without owner
  revoke and `wire edge flash` records window duration. The storage-path users
  of the stop/reset helper family are governed by the apply-liveness design
  below.
- The RP2040 wear-leveling backing store does not mask core0 interrupts across
  its program and erase windows. That mask covers one hazard — the SSI leaves
  memory-mapped XIP to issue the command sequence, so flash-fetched code must
  not run — and this image has no flash-resident code left to fetch, which the
  vector table residency above is what makes literally rather than nearly
  true. **The removal is gated on the `ERA_SRAM_RESIDENT_IMAGE` marker rather
  than being a deletion**, because any keyboard elsewhere in this fork that
  does not include the residency rules is an XIP build where the mask is
  load-bearing, and the marker is what keeps the mask and the residency
  changing together rather than in two places. The marker is defined beside
  `MCU_LDSCRIPT` in `system/era_sram_resident_rules.mk` and storage v1 asserts
  it at compile time; what else it selects is canonical in
  `era_board_adoption.md`'s **Copy-To-RAM Policy**. Both windows are gated, not
  only the erase, and the guarantee that unreachable slots stay unreachable
  moves from `PRIMASK` to the NVIC enable bits — weaker, which is exactly why
  the slots had to move to SRAM first rather than the other way round. What
  the removal buys is correctness plus removed ISR latency across the ~821
  write windows of a CLEAN burst and every ordinary VIA write. It is **not**
  an enumeration fix, and that is recorded because it is the reason it was
  argued for: a measured CLEAN enumeration found `route slave` unmoved.
- **Boot-ordering invariant, load-bearing and recorded here.** The
  wear-leveling init consolidation and the hardware-id unique-ID read are the
  only unhooked flash operations and both run inside `keyboard_setup`
  (`quantum/main.c:41`), strictly before the first possible core1 launch. **No
  earlier core1 launch entry point may be added.**

  The launch site is keyboard post-init: `era_split_keyboard_post_init()`
  calls `era_split_transport_scheduler_start_communication_core()`, the one
  boot entry point that opens the wire. `keyboard_post_init_kb` is the last
  step of `keyboard_init()` (`quantum/main.c:44`), so the launch is later than
  `keyboard_setup` by a wide margin, and later than `matrix_init()`,
  `quantum_init()`/`eeconfig_init()` and `rgb_matrix_init()` as well.

  The division that keeps this true, and it is the part a review looks for in
  the wrong place: the scheduler computes policy without opening the wire, and
  the named post-init step performs the launch.
  `era_split_transport_scheduler_init()` (`split/era_split_transport_scheduler.c`) may run from either transport hook or
  from neither, and does not decide when core1 starts — a launch that is a
  side effect of an authority sample lands ~86 lines of init away from where
  this contract says to look. `ensure_core1()` still appears on the scan path
  and the host-peer lanes; those are re-arms of an already-launched core, not
  launch entry points, because no matrix scan and no housekeeping pass runs
  inside `keyboard_init()`.
- Two ELF facts the obvious reading of crt0 gets wrong. `__init_ram_areas()`
  does **not** zero `.bss`: `ram_areas` entry[0] has `init == clear ==
  no_init`, so both of its loops run zero iterations, and `.bss` is zeroed by
  crt0's own separate loop (`crt0_v6m.S:267-281`). `__init_ram_areas()` zeroes
  `.ram5` (entry[5], `0x20041000..0x200418BF`, 2240 B = core1's 192-byte
  vector table plus its 2048-byte stack); that entry's copy sub-loop is
  likewise a no-op because `.ram5_init` is empty.

## Apply-Liveness Design

The durable apply holds no core1 stop across the EEPROM write, so the single
wire-anchored 100 ms liveness rule has no exemptions anywhere. Exact protocol
semantics — cursor state, abort ordering, the core1 beat, and both storage
state machines — are canonical in `era_host_peer_storage_contract.md`. What
this contract owns is the placement-side consequence and the bound:

- The PEER durable apply is a sliced write: a cursor state writes bounded
  sub-ranges (span-diffed against the wear-leveling cache) from the
  already-CRC-validated staged image, driven from the cold storage task with a
  pending-work condition so slices run back-to-back. Core0 returns to its loop
  between slices; the matrix-scan freeze drops from the full write duration to
  one slice bound.

  **That bound is structural, not small, and this is the sentence that must
  not be read as reassurance.** One slice is 32 bytes, but a slice that
  triggers a wear-leveling erase or consolidation blocks core0 for the
  hardware duration — hundreds of milliseconds have been measured inside
  single slices, and the consolidation worst case is an explicitly unmeasured
  standing debt (`era_performance_gates.md` carries both figures). The
  applying half does not scan for that window, and no gate observes it,
  because the scan-rate gates never run during an apply.
- Core1 stays running through the write, and **what feeds the peer's watch
  during the window is core1's standing-exchange liveness beat, in both
  serviced relations.** There is no core0 keepalive and there must not be:
  core0 is the thing the write blocks, so a keepalive emitted from it is not a
  liveness signal. This states the mechanism rather than a measurement; the
  HOST-PEER half of it is unmeasured.
- The rest of the apply protocol belongs to the storage contract and is named
  here only so it is recognizable: an abort while the cursor is open finishes
  the local sliced write of the validated staged image before aborting the
  wire episode, so **no torn-EEPROM terminal state exists**, cable-pull
  standalone included; the `nvm_eeprom_changed_kb` suppression covers the
  apply's own slice writes only, so a foreign write into the target domain
  still raises the dirty-abort; every exit clears the storage apply guard and
  the guard-ignorant core1 stoppers (diagnostics flush, mode-change serial
  reset) are interlocked against an open cursor; and the episode closes
  through one brief `rotate_storage_relation` (1-4 ms of wire silence) that
  provides the owner-epoch discontinuity and forced session revalidation.

**Consolidation stance, and it is a decision-record.** The wear-leveling
consolidation erases the whole 48 KB backing in one call that blocks core0 for
its duration, and can exceed the 100 ms watch on either half, in or out of
storage episodes. That call no longer masks interrupts on this image (above),
so USB and the timers survive the window; what does not is core0's own
scheduler pass, and that is what the watch can still trip on. A consolidation
that lands mid-apply or mid-session is an honest stale-trip-and-recover event,
never a liveness exemption: the watch fires truthfully, write-through
completes the local image, and the reopen audit converges (worst case one
session re-establishment, with the content already durable). Per-sector
consolidation decomposition remains an evidence-gated option.

> **REFUSED:** a pre-flight log-headroom gate ahead of the write.
> **WHY:** it needs a QMK-common accessor, and it converts a mid-write trip
> into a pre-write abort that leaves the domain un-applied — strictly worse
> than write-through.
> **REOPENS:** a headroom read that costs no new accessor, on a store where
> the abort would leave no domain un-applied.

> **REFUSED:** decomposing the wear-leveling consolidation further than the
> sliced durable apply above, and making the durable write asynchronous.
> **WHY:** the sliced apply measured sufficient against the freeze target on
> its own. Decomposition
> buys 68 → ~32 ms and opens a torn-consolidation hazard slicing does not
> carry: `wear_leveling_write_consolidated()` (`quantum/wear_leveling/wear_leveling.c`) computes its FNV checksum from
> the live cache *after* the bulk write, so a cache mutation inside that gap
> stores a torn image under a checksum describing the untorn one and the next
> boot clears the store on the mismatch — the interlock would have to defer or
> snapshot rather than apply, a cost priced nowhere else. Of the two
> asynchronous designs only one is architectural: poll-with-yield removes none
> of the slicing's
> mechanisms, since core0 still calls its own keyboard task from inside the
> storage layer, and buys the case the 32 ms acceptance declined;
> issue-and-return removes the call-graph cycle at the price of a durability
> change (`eeprom_write_block()` returning before flash) plus a restructure of
> `wear_leveling_consolidate_force()` and the QMK EEPROM contract above it.
> **REOPENS:** the 32 ms interval becoming a measured complaint, or the
> call-graph cycle producing another defect.

## Closed Placement Surfaces

Do not reopen without a new active measured plan:

- any per-function RAM/flash placement macro layer (hot islands,
  RAMFUNC/not-in-flash wrappers, custom code sections) in ERA or QMK
  common code;
- non-striped main-SRAM bank partitioning or double-mapped bank aliases;
- XIP cache reclaim as SRAM (the recorded 16 KiB reserve above, unused);
- moving shared SPSC rings or publication words into a scratch bank
  (scratch banks stay core-private; shared records stay in main SRAM);
- QMK core matrix edits.

Migrating additional core1-private mutable state into the SRAM5 remainder is
not closed; it is evidence-gated on measured bank contention.

## Verification

- `common/tools/era_residency_gate.sh` is the whole residency check: ram0
  residency is the sum of ALLOC sections with ram0 VMAs **excluding the
  elastic `.heap`** (ChibiOS filler, not a requirement), and at least 32 KiB
  of 262,144 B must be free; allocator absence is asserted
  (`malloc`/`_sbrk`/`chCoreAlloc*`/`chHeapAlloc` undefined in the ELF); and the
  linked `.vectors` table is read. It takes an ELF and nothing else — no board,
  no profile, no build — and the tomak79h launcher calls it rather than
  carrying a second copy, so the three manifest fields a profile records are
  the three any board's adoption produces. Board-agnostic is not a
  convenience: while the check lived inside that one launcher,
  `newone/odessey60s` linked `malloc` through an RGBLIGHT effect for ten days
  before anyone ran the allocator check by hand.
- One paced wire-diagnostics line reports `chCoreGetStatusX()` against the
  `__heap_base__..__heap_end__` span once per capture, proving the elastic
  heap stays at the platform baseline at runtime.
- The standing gate for any change to this layout is the Layout Gate in
  `era_performance_gates.md`, which owns the procedure and every measured
  comparison figure.
