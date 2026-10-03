# ERA Active Invariants

Genre: contract
Canonical for: non-negotiable ERA cross-cutting invariants — QMK fork hygiene,
the HID boundary, the core0/core1 lifetime and thread boundary, boot safety,
source-structure boundaries, persistent identifier compatibility, and tripwires.

Wire ids, section shape and eligibility: `era_wire_contract.md`. Relation and
session authority: `era_authority_contract.md`. Route timing and standing
exchange: `era_route_contract.md`. Placement, vectors and flash carve-out:
`era_sram_residency_contract.md`. The full current QMK-core edit surface:
`manuals/era_qmk_fork_ledger.md`.

## QMK Fork Hygiene

- `quantum/matrix_common.c` MUST remain byte-identical to pristine QMK
  `c93ef27143`. Verify against that baseline, never against another ERA branch.
- `quantum/matrix.c` may carry exactly two ERA departures: the stock-matrix
  raw/count diagnostic hooks, and the ordered `debounce()` then
  `matrix_post_scan()` split. The detailed file set and selectors are owned by
  `manuals/era_qmk_fork_ledger.md`; the Source Gate is in
  `manuals/era_performance_gates.md`.
- A third matrix hook is closed unless its selector and a real consumer land in
  the same change. A dormant hook is an unowned fork departure.
- The debounce/post-scan calls MUST remain separate statements. Recombining them
  into one `|` expression is forbidden because C does not specify operand
  evaluation order.
- RP2040 ERA builds use the ERA custom matrix path. `sirind/brick65` is the
  atmega32u4 exception. First selectors: `system/era_common_qmk_rules.mk` and
  `split/era_split_qmk_rules.mk`.

## HID And Runtime Separation

- Split transport MUST NOT inject HID events or create a remote HID endpoint.
  Received state may update caches and the local matrix projection only. First
  source anchors: `split/era_split_transport.c` and
  `system/era_rp2040_matrix_core.c`.
- **render-state clause:** visual or resolution state that contains pressed
  positions remains render state. It MUST NOT write matrix state or reach action
  processing. Wire eligibility is owned by `era_wire_contract.md`.
- Applying a new debounce policy MUST reconcile every pending row on the first
  scan under the new policy even without another raw edge. A pending release
  may not remain pressed indefinitely, and the converged idle path stays
  clock-free. Production coverage: `tests/era_matrix_debounce_runtime`.

## Core Lifetime And Thread Boundary

- `CORE1_FULL` is the only ERA split stage. Every active split backend RX/TX
  operation executes on core1. Core0 owns relation/route policy, live QMK state,
  semantic publication, and result apply; a core0 wire executor or synchronous
  fallback is closed.
- While core1 owns a transaction, it also owns control-byte construction and
  tx/ack sequence state. Core0 may publish semantic immutable requests but may
  not preallocate wire sequence values. First anchors:
  `split/communication_core/` and `split/era_split_transport_scheduler.c`.
- Route authority and exchange timing are separate. Core0 grants a bounded
  relation/route execution right; core1 may schedule only inside that grant.
  A stopped grant does not self-resume after failure. Exact stop semantics:
  `era_route_contract.md`.
- A bare RP2040 core1 actor MUST NOT call ChibiOS thread suspend/resume or
  scheduler-lock APIs. PIO IRQ/event wake is only a hint; FIFO, error, deadline,
  cancel and owner-epoch predicates remain receive authority. First anchor:
  `split/era_split_transaction_backend_rp2040.c`.
- Cross-core transport MUST use bounded static ERA SPSC storage with explicit
  ordering. ERA images may not introduce allocator-backed queues, hardware
  spinlocks as the transport lock, or the inter-core FIFO as the data path.
  Pico SDK queues allocate, spinlock helpers can mask interrupts while held,
  and the inter-core FIFO is reserved by the launch handshake.
- Before a success response publishes new core0 work, bounded responder-result
  capacity MUST already be reserved. Accepted matrix/source data may not be ACKed
  before it is stored. Runtime duplicate result reuse requires an already-pending
  result with the same owner/relation/snapshot/section identity. Input instead
  reserves its own packet/baseline capacity, copies and release-publishes before
  ACK, and retains a receipt even after consumption. Input pins must not block
  runtime plan publication. First input owner: `split/era_split_input.c`.
- Forced processor halt does not stop PIO. A new wire owner must reset PIO TX/RX
  PC, shift/count/divider/FIFOs and partial-frame state before READY. Producer
  incarnation and epoch reuse additionally require typed FENCE/BIND boundaries;
  a live packet/baseline holder cannot be overwritten or relabeled.
- Whole snapshot action admission checks remote quota and HID pressure before
  projection and any action side effect. Closure releases obey the same rule.
  Input reset never clears already accepted mixed QMK/HID work. Actual whole
  USB admission, under the Core0 USB system lock, is the only successful report
  post; normal shared-endpoint BUSY cannot erase accepted reports.
- ERA NVM program/erase remains synchronous core0 work and MUST NOT recurse into
  keyboard, matrix, scheduler, or transport execution. Durable-apply and
  maintenance bounds are owned by `era_host_peer_storage_contract.md`.

## Boot Safety

- Core1 MUST be hardware-halted at the entry to the pre-copy window that can
  destroy the image or state core1 is executing. Reset-cause enumeration is not
  an adequate substitute because debugger/SWD resets also rerun crt0. First
  anchor: `system/era_boot_core1_halt.c`; placement pins:
  `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`.
  `NVIC_SystemReset()` asserts `AIRCR.SYSRESETREQ`; on RP2040 that resets only
  the asserting core, so it cannot stand in for halting core1 (RP2040 datasheet
  2.4.2.9; the broader 2.4.8 wording is not sufficient for this boundary).
- The core0 SIO FIFO IRQ is masked across the core1 launch handshake and restored
  afterwards. ChibiOS also owns that FIFO after startup; two consumers may not
  race the handshake.
- Before crt0 copy/clear, the only production SRAM object firmware may read or
  write is the one-word `magic_location` double-tap state in its dedicated
  linker region. Watchdog reason/scratch MMIO is not a second SRAM exception.
  Adding another retained SRAM object requires a named region outside every
  copy/clear range and an exact-size linker assertion.
- Every pre-copy wait is bounded. The PSM acknowledge bound remains
  `ERA_BOOT_CORE1_HALT_ACK_SPINS` 1000, and the halt clears `FRCE_OFF` before
  return so the later launch handshake remains possible.
- After crt0, executable paths may not retain a flash VMA. Every vector entry
  except the reset slot points to SRAM; the reset slot stays in flash because
  boot2 fetches it before the image copy. Verification and the interrupt trade
  are owned by `era_sram_residency_contract.md` and
  `manuals/era_performance_gates.md`.
- Ordinary boot has no reset-cause branch. The bounded physical-RUN classifier
  may change only the double-tap state; it may not gate core1 halt, image copy,
  initialization, or USB attach. ERA defines no `USB_WAIT_FOR_ENUMERATION`;
  USB attach stays on ordinary boot and may not be deferred behind a
  housekeeping-loop failsafe, because a failsafe that depends on the loop it
  protects is not fail-safe before that loop runs.
- Stable physical-release qualification remains 184,320 ROSC ticks:
  `ERA_BOOT_GUARD_ROSC_CHUNK_COUNT` 768 x
  `ERA_BOOT_GUARD_ROSC_CHUNK_TICKS` 240, with each chunk bounded by
  `ERA_BOOT_GUARD_ROSC_POLL_LIMIT` 256 polls. The window MUST NOT reconfigure
  ROSC, XOSC, dividers, frequency, or the clock tree. ERA RP2040 boards keep
  `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT` 1000; an observed total up to
  2000 ms remains permitted. Source: `system/era_boot_core1_halt.c`.
- A timestamp taken before `keyboard_init()` may not be compared with one after
  it because QMK reinitializes the timer. Post-init double-tap close therefore
  carries an explicit started bit; timer value zero is valid and not an unset
  sentinel. Source: `platforms/chibios/bootloaders/rp2040.c`.
- Every deliberate software exit from the running image clears double-tap magic.
  Physical-tap classification occurs before crt0 copies SRAM; the bootrom jump
  occurs only after that copy. Both `mcu_reset()` and `bootloader_jump()` disarm
  independently. This separation is required because the bootrom jump code is
  SRAM-resident and cannot be called before the copy.
- The first core1 launch is a single post-init step. Transport initialization is
  policy-only and MUST NOT open the wire. Source boundary:
  `split/era_split_keyboard.c` ->
  `split/era_split_transport_scheduler.c`.
- Runtime quiesce is reversible parking and is NOT a substitute for the hardware
  halt. A following boot can clear runtime flags while rewriting core1 state;
  therefore boot safety cannot depend on quiesce.

## Source Structure

- A structural change is justified by unreachable code, an over-split unit with
  no distinct owner or compilation reason, or an under-split unit carrying
  multiple owners, eras, vocabularies, or duplicated logic. File length alone
  is not a defect. Where source/link order can affect placement, inlining, or
  core ownership, inspect the current call graph or disassembly before changing
  the boundary.
- Candidate-only structural scanners remain uncommitted unless each reported
  item is a tree fact rather than a heuristic candidate. A locatability failure
  qualifies because the missing target is mechanically decidable.
- `split/era_host_peer_storage.c` and
  `split/communication_core/era_split_communication_core_storage.c` remain
  unsplit until a proposed cut avoids moving shared helpers or shared state
  across the translation-unit boundary. Re-derive the current call graph for a
  proposal; historical helper/count measurements are not contractual evidence.
- The defensive `default:` on the lane switch in
  `split/communication_core/era_split_communication_core_host_peer_lanes.c`
  remains required while the cross-core lane field is byte data. Enum
  exhaustiveness cannot validate an arbitrary `uint8_t` value.
- Do not reintroduce compatibility wrappers for the deleted master-scheduler or
  sync-lane layers. A wrapper would recreate the ambiguous ownership removed by
  the replacement lane.

## Identifier Compatibility

- A numeric identifier interpreted outside one build — including wire/storage
  protocol ids, persisted VIA definitions, and diagnostic/capture ids — MUST NOT
  be silently recycled after retirement. Releasing one requires the owning
  compatibility contract plus affected decoders/fixtures to change together.
- A USB identity (VID, PID) is such an identifier: host drivers, browser
  WebHID grants, the official VIA registry and the ERA app all match on it.
  Each maker owns one VID; the VID block, the PID sequence and the split-half
  PIDs are allocated by `the-via-eerraa/docs/adr/0004-firmware-distribution.md`.
  The formerly shared ERA VID `0x4552` and every PID issued under it are
  retired and are never assigned again, not even to the product that held
  them. A board several makers sell is built once per maker, and those images
  differ only in USB identity and manufacturer string: no firmware logic may
  depend on the VID or PID value. The live values are each target's
  `keyboard.json` `usb` block and a split board's
  `ERA_SPLIT_USB_IDENTITY_PID_LEFT` / `ERA_SPLIT_USB_IDENTITY_PID_RIGHT`;
  `tests/era_firmware_version/test_definitions.py` pins them and refuses a
  repeat. The `0x4552…` words such as `ERA_EEPROM_RESET_KEY` are ASCII storage
  magic, not USB identities.
- A new VIA channel/value claim MUST be checked against every live claimant in
  that namespace. Dispatcher order does not make an unintended overlap safe;
  an overlap is valid only when the owning build/feature contract makes the
  claimants mutually exclusive.
- Mode values 5 and 6 (formerly `HOST_HOST_LEFT` / `HOST_HOST_RIGHT`) remain an
  unresolved compatibility conflict: `split/era_split_mode_planner.h` currently
  says 5 is available, while the retired-capture policy previously reserved
  both. Do not allocate either in an unrelated change; an explicit owning
  compatibility decision must reconcile the source statement and this contract
  first.

## Official VIA Definitions

- Stock VIA (usevia.app) keeps the basic controls it can support correctly and
  simply. Features requiring unsupported behavior, complicated explanations or
  awkward UI are reduced in official JSON and supported in the Custom app at
  usekb.cc, with guidance in usevia.txt. Preserve existing wire meanings; equal
  UI exposure is not a requirement. Stock LINK keeps selection and Apply but
  omits runtime, saved and result TEXT because they cannot stay current there.
  Keep their read-only wire handlers for Custom observation; do not restart
  firmware merely to refresh the stock screen.
- Tap Dance follows this shared QMK/H7S policy: stock VIA edits only a
  slot already in Legacy mode (0), through its four actions and Term presets.
  Input modes 1/2, independent hold timing and hold-on-other-key settings belong
  to the Custom app at usekb.cc. Stock menus cannot adequately express the
  advanced inheritance and timing dependencies; partial editing could change
  their meaning. Official definitions retain a hidden read-only
  mode GET, show only the app guidance for advanced or unknown modes, and
  expose no mode SET or advanced timing control. They must not reinterpret or
  overwrite advanced slots as Legacy. Existing VIA GET/SET/SAVE wire support
  remains available to the Custom app; no app-specific protocol is introduced.
  `tests/era_firmware_version/test_definitions.py` checks this boundary, including
  deliberately broken definitions.
- Lighting lives under a top-level `Lighting` menu whose submenus are named for
  where the LEDs are (`Per-Key RGB`, `Underglow`, `Backlight`, `Badge`, Riley's
  `RGB LEDs`), never for the engine. Lock indication is chosen by `Indicator` /
  `Indicator N` dropdowns whose value 0 reads `RGB Effect` where the LED also
  shows the effect and `Off` on a dedicated LED. RGB Matrix effect lists are
  explicit and follow the firmware mode order, because VIA's built-in list does
  not match the enabled effects. `tests/era_firmware_version/test_definitions.py`
  enforces this; H7S uses the same names.
- An official JSON's `vendorId`/`productId` equal the USB identity its target
  enumerates with (on a split board, the L JSON the left half's and the R JSON
  the right half's), because stock VIA pairs a draft definition with the
  device by that pair.
  Each maker target of a shared board body ships its own copy, identical to
  the other makers' except for those two fields.
  `tests/era_firmware_version/test_definitions.py` enforces both.

## Stop Conditions

Stop and require an explicit owning-contract change before implementing any of
these:

| Tripwire | Owner that must change first |
| --- | --- |
| a new QMK matrix-core departure | this section plus `manuals/era_qmk_fork_ledger.md` |
| direct HID/action injection from split transport | **HID And Runtime Separation** |
| serial backend execution on core0, live QMK/EEPROM ownership on core1, or a core1 ChibiOS scheduler dependency | **Core Lifetime And Thread Boundary** |
| a second pre-copy SRAM object, unbounded pre-copy wait, reset-cause boot fork, or quiesce replacing the halt | **Boot Safety** and `era_sram_residency_contract.md` |
| a new split payload, direction, route, response slot, or storage surface | `era_wire_contract.md`, `era_route_contract.md`, `era_closed_surface_contract.md`, or `era_host_peer_storage_contract.md` as applicable |
| a source split/merge or wrapper that crosses the **Source Structure** boundary | **Source Structure** |
| reuse or allocation of an externally interpreted numeric identifier, including mode 5/6 | **Identifier Compatibility** plus the owning protocol/VIA contract |
