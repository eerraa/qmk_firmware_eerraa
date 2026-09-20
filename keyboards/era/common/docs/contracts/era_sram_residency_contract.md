# ERA SRAM-Resident Load Image Contract

Genre: contract
Canonical for: ERA RP2040 copy-to-RAM execution, the flash startup boundary,
SRAM ownership and budget requirements, reset/copy/Core1-launch ordering,
Core1 stack/vector residency, durable-flash liveness, and closed placement
surfaces

The exact MEMORY regions, addresses, section memberships, startup selectors,
and linker assertions are owned by
`keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`. Build selection is owned by
`system/era_sram_resident_rules.mk`. Core/thread ownership is owned by
`era_invariants.md`; storage authority and the ERA NVM capacity/atomicity
contract are owned by `era_host_peer_storage_contract.md`. Acceptance
procedures are in `era_performance_gates.md`.

This contract applies to ERA RP2040 targets. The atmega32u4 Brick65 path is
outside this load-image contract.

## Execution and placement boundary

ERA RP2040 firmware uses one copy-to-RAM runtime image. After crt0 completes
its copies, runtime code/data/rodata and the live vector table execute from
SRAM; the reset/bootstrap material that must run before the copy remains in the
linker's flash startup carve-out. The flash load image and linker-reserved ERA
NVM region remain flash data, not runtime instruction-fetch dependencies.

Exact placement values are intentionally not repeated here. Read the linker
script for region sizes/addresses, section VMAs/LMAs, the startup object
selectors, and the NVM overlap assertions.

The flash startup carve-out is a closed pre-copy execution island:

- every function and constant reachable before the runtime image is copied must
  be available from that carve-out;
- a pre-copy call or constant read into the not-yet-copied SRAM image is a boot
  failure even if the linker can satisfy it with a veneer;
- linker assertions prove selected symbols are placed, but do not prove the
  transitive call/data closure; `era_performance_gates.md` **Layout Checks**
  owns that review;
- object-anchored placement is incompatible with LTO until a measured design
  provides selectors and closure proof that remain valid under LTO.

The XIP cache reclaim remains an unused reserve. Reclaiming it, or introducing
a second RAM/flash placement scheme, requires a new measured plan.

## SRAM ownership and stack boundary

SRAM residency grants no new access authority. Core1 still has no live
QMK/RGB/VIA/EEPROM/USB/HID access and no EEPROM write authority; those
ownership rules remain in `era_invariants.md` and
`era_authority_contract.md`.

Main SRAM holds the copied runtime/shared state. Scratch banks remain
core-private: Core0 stacks stay on the Core0 scratch allocation, while the ERA
Core1 stack and private vector table stay on the Core1 scratch allocation.
Shared SPSC/publication records remain in main SRAM rather than moving into a
core-private scratch bank. Exact addresses and linked sizes are linker/source
facts.

The Core1 reservation is the linked
`g_era_split_communication_core_stack` size in
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`.
Its source default, compile-time measured minimum, vector count, and current
single-image measurements are not restated here. A change that adds Core1 call
depth must be re-walked with `common/tools/era_core1_stack_walk.py <elf>`;
acceptance is positive headroom against the reservation in that same linked
image. Per-variant measured baselines live only in
`era_performance_gates.md`.

## Reset and boot ordering

The order below is load-bearing:

1. Reset enters the flash startup path. Before crt0 overwrites or clears runtime
   SRAM, `early_hardware_init_pre()` in `system/era_boot_core1_halt.c`
   performs the pre-copy reset work and halts Core1 so no old Core1 execution
   survives the destructive copy/clear window.
2. crt0 copies the live vector table and runtime image, clears the ordinary
   runtime regions, and retargets VTOR as defined by the linker/vendor crt0.
3. `keyboard_setup()` mounts ERA NVM, and the split pre-init path reads the
   hardware unique ID, before the first possible Core1 launch.
4. The first Core1 launch occurs only from the post-init path after those boot
   facts exist. **No earlier Core1 launch entry point may be added.**

The launch/policy lifetime is further constrained by `era_invariants.md`.
Call sites are `storage/era_eeprom_driver.c`,
`split/era_split_usb_identity.c`, `split/era_split_keyboard.c`, and
`split/era_split_board.c`; the QMK boot call chain is in
`quantum/keyboard.c`.

## Pre-copy reset state

The production pre-copy SRAM exception is one retained double-tap magic word
outside crt0's copy/clear ranges. Adding another pre-copy SRAM object reopens
the boot-safety invariant. Its exact section name, placement, size assertion,
and symbol equality are linker facts in
`keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`.

`early_hardware_init_pre()` may depend only on reset-domain MMIO, the retained
magic, and stack locals before the copy. It must not call into the SRAM runtime
image, and every wait in this window must be bounded. Exact guard constants,
poll limits, and reset-domain fields are owned by
`system/era_boot_core1_halt.c`; they are not duplicated here.

The firmware has no calibrated pre-clock timebase for measuring this pre-copy
window. Source arithmetic is not device timing evidence; any future timing
claim for the physical double-tap path requires an external measurement.

## Vector residency and Core1 launch

During runtime flash program/erase, executable/fault paths must not require
XIP. The reset vector remains the bootstrap exception in flash; the other live
vector entries must resolve to SRAM. The authoritative linked-table check is
`common/tools/era_residency_gate.sh`; exact table size and address ranges are
checker/linker facts.

ERA Core1 uses a private copied vector table and private stack from
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`.
A first launch, and a launch after declared death, rebuilds the Core1 vector
state from the current live table; waking an already-launched Core1 does not
create a second ownership path.

## Durable flash liveness

ERA NVM program/erase is Core0 work. A durable Apply must not hold Core1
stopped across the flash transaction; relation liveness remains Core1's
standing-exchange responsibility. Conversely, Core0 cannot provide a keepalive
while it is the actor blocked by the flash operation.

Storage admission, old-or-new durability, repair-forward behavior, background
maintenance, and the prohibition on recursive keyboard/wire work inside an
NVM primitive are owned by `era_host_peer_storage_contract.md`.

## Closed placement surfaces

Do not reopen these without a new active measured plan:

- per-function RAM/flash islands or RAMFUNC/not-in-flash placement layers in ERA
  or QMK common code;
- non-striped main-SRAM partitioning or double-mapped bank aliases;
- XIP-cache reclaim as general SRAM;
- moving shared publication/SPSC state into a core-private scratch bank;
- placement-motivated QMK core matrix edits.

Moving additional Core1-private mutable state into unused Core1 scratch space
is not closed, but requires measured bank-contention evidence.

## Verification

`common/tools/era_residency_gate.sh <elf>` is the enforced linked-image gate
for every ERA RP2040 image. It must fail rather than report success when it
cannot measure the target.

The standing requirements are:

- at least **32768 bytes of main SRAM free** after counting linked resident
  allocations under the gate's method; this floor applies to every variant;
- no allocator entry point accepted by the gate may be linked into the image;
- the reset vector must remain in the flash bootstrap range and every other
  live vector entry must resolve to SRAM;
- the gate consumes the linked ELF itself, not a board-name/profile argument.

Exact memory bounds, vector count, allocator symbol set, and output field names
are owned by `common/tools/era_residency_gate.sh`. The complementary
placement/closure/magic-word checks are
`era_performance_gates.md` **Layout Checks**. Core1 call-depth changes use
`common/tools/era_core1_stack_walk.py <elf>` and compare against the linked
reservation from the same image.

> **REFUSED:** making these checks a single board's launcher responsibility.
> **WHY:** SRAM/XIP safety is a load-image property shared by every ERA RP2040
> target.
> **REOPENS:** none; the gate remains ELF-driven and board-neutral.
