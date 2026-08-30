# ERA SRAM-Resident Load Image Contract

Genre: contract
Canonical for: ERA RP2040 memory placement and the load layout, copy-to-RAM
execution scope, the flash startup carve-out, the SRAM budget structure and
growth reserves, the boot-ordering invariant, the durable-apply liveness
design, and the closed placement surfaces

## Why The Image Is Resident

QSPI NOR cannot serve instruction fetches during program/erase; the SSI leaves
XIP to issue commands, so a core executing from flash must stop for a commit.

| Reserve | Size | Note |
| --- | --- | --- |
| XIP cache reclaim | 16 KiB | unused; recorded reserve |
| LTO | off | object-anchored carve-out selectors stop matching; needs its own measured gate before adoption |

The standing free-RAM requirement is the 32768 floor under **Verification**.
The wire-diagnostics variant is the ram0-resident worst case; the floor applies
to every variant.

## Load Layout

The whole firmware image executes from SRAM on both cores (copy-to-RAM).
Scratch banks are per-core private: core1 stack and vector fetches never
contend with core0 in striped `ram0`. Regions from
`keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`.

| Region | Address / size | Holds |
| --- | --- | --- |
| flash (kept) | `flash1` default 2048k (`0x10000000`); 128 KiB NVM at the end | `.boot2` `0x10000000`; `.vectors` LMA `0x10000100`; `.flash_startup`; `.sram_image` LMA; ERA NVM region |
| `ram0` | `0x20000000`, 256 KiB striped (262144 B) | vector table, copied image, `.bss`, `.era_bootloader_magic`, elastic `.heap`; ≥32768 free |
| `ram4` SRAM4 | `0x20040000`, 4 KiB | core0 `.mstack` `__main_stack_size__` 0x400 and `.pstack` `__process_stack_size__` 0x800 from `platforms/chibios/platform.mk` (RP2040 does not override). ERA places no `.ram4*` object |
| `ram5` SRAM5 | `0x20041000`, 4 KiB | core1 stack `ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS` 512 (2048 B) plus 48×4 = 192 B vector table in `.ram5_clear` = 2240 B at `0x20041000..0x200418BF` |
| `ram7` | `0x20041f00`, 256 B | ROM scribble during reset |
| unclaimed | 1600 B | `0x200418C0` .. `0x20041f00`, below `ram7` |

## Ownership Does Not Move With Placement

**SRAM residency grants no new access.** Core1 still has no live
QMK/RGB/VIA/EEPROM/USB/HID access and no EEPROM write. Ownership lists:
`era_invariants.md` and `era_authority_contract.md`.

## Scheduling Discipline

Placement relaxes none of these. File ownership: `era_source_map.md`.

| Rule |
| --- |
| Storage capture, EEPROM read/write, CRC, comparison, route enqueue, result drain, replacement Apply, runtime reload, and diagnostics execute only at task/housekeeping boundaries, never inside matrix scan |
| Matrix-scan transport reads cached scalar facts only |
| Diagnostics formatting and snapshot construction stay outside matrix-scan hooks and are explicitly paced |
| Role lifecycle, send, response-window RX, and responder-idle RX execute only under the core1 owner epoch; IRQ code does not drain or validate frames |

## Load Image Scope

Layout: `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`, selected with
`MCU_LDSCRIPT` in `system/era_sram_resident_rules.mk`. Split boards take that
include from `split/era_split_qmk_rules.mk`; every other RP2040 board includes
it from its `post_rules.mk`. `sirind/brick65` (atmega32u4) takes none of it.
One finished layout. No remaining XIP path on RP2040.

The bundle also compiles `storage/era_nvm.c` and `storage/era_nvm_rp2040.c`.
`storage/era_nvm_rp2040.c` `#error`s unless `ERA_SRAM_RESIDENT_IMAGE` is defined.
`system/era_sram_resident_rules.mk` emits the link map unconditionally, defines
`RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM`, and `$(error)`s unless
`ERA_COMMON_QMK_RULES_INCLUDED` and `ERA_BOARD_COMMON_ENABLE` are already yes
(the class skeleton's `housekeeping_task_kb` is the only closer of the
double-tap window this bundle arms). Closer hazard:
`era_board_adoption.md`'s **Copy-To-RAM Policy**.

Flash keeps only `.boot2` at `0x10000000`, the `.vectors` LMA at exactly
`0x10000100` (boot2 hardcodes the reset MSP/PC fetch), `.flash_startup`, the
`.sram_image` LMA, and the 128-KiB ERA NVM region.

Six pinned TUs. A bare `*board.o` suffix is refused. Selectors and the three
addition rules (silent flash→SRAM veneer, mandatory `.rodata` arm,
`Reset_Handler`→`_crt0_entry` order) live at the selector list in
`keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`. Keep the default `BOARD`, or
amend the carve-out in the same change.

| TU | Pin |
| --- | --- |
| `crt0_v6m.o` | `.text` |
| `vectors.o` | `.text`; adjacent to crt0 |
| `crt1.o` | `.text` + `.rodata` |
| path-anchored `RP_PICO_RP2040/board.o` | `.text` + `.rodata` |
| `chibios.o` | `__early_init`, `early_hardware_init_pre`, `early_hardware_init_post` only (`tmk_core/protocol/chibios`) |
| `system/era_boot_core1_halt.o` | last; `.rodata` arm; tighter `ASSERT` |

`early_hardware_init_pre()` in `system/era_boot_core1_halt.c` is the whole
pre-copy window. The `ASSERT`s catch a dropped `BOARD` or `SRC`; **closure
itself is not caught**. The objdump/nm review is `era_performance_gates.md`
**Layout Checks**.

The veneer set, not a byte count, is the gate: `__main_veneer` and
`____late_init_veneer` serve the post-copy `bl __late_init` and `bl main` in
stock crt0. A third veneer is the pre-copy-caller signature. Pinning an
existing TU costs negative SRAM; pinning a flash-only TU costs zero.

| Symbol | Must link inside |
| --- | --- |
| `Reset_Handler`, `_crt0_entry`, `__cpu_init`, `__early_init`, `__chibios_override___early_init`, `early_hardware_init_post` | `__flash_startup_base__` / `__flash_startup_end__` |
| `early_hardware_init_pre` | `__era_boot_core1_halt_base__` / `__era_boot_core1_halt_end__` |
| `era_unhandled_vector` | SRAM `.text` (`__text_base__` / `__text_end__`) |

Necessary, not a closure proof.

Everything else lands in one `.sram_image` (ram0 VMA, flash LMA), copied by
stock crt0 `CRT0_INIT_DATA` via redefined
`__textdata_base__`/`__data_base__`/`__data_end__`; `.vectors` copies through
`CRT0_INIT_VECTORS`, which then retargets VTOR. The ERA script INCLUDEs the
stock ChibiOS stack and memory rules and does not INCLUDE the ChibiOS core1
stack reservation, consistent with `CRT0_EXTRA_CORES_NUMBER` 0 in
`platforms/chibios/vendors/RP/RP2040.mk`, because ERA launches core1 itself.

Accepted residual: `CRT0_VTOR_INIT` 1 in that same makefile points VTOR at the
not-yet-copied SRAM table for the few hundred pre-copy cycles. Accepted to keep
the vendor makefiles' `CRT0_*` set.

> **REFUSED:** a bare `*board.o` carve-out selector.
> **WHY:** it matches five objects in a split build, and a different five in a non-split build, and would pin keyboard code into flash.
> **REOPENS:** a selector that names only `RP_PICO_RP2040/board.o`.

> **REFUSED:** a second pinned pre-copy translation unit to split the halt from the double-tap arm.
> **WHY:** a dropped selector becomes a silent veneer into uninitialized SRAM with no fault handler.
> **REOPENS:** a linker error, not a veneer, when a pre-copy caller reaches `.sram_image`.

> **REFUSED:** `LTO_ENABLE=yes` with the current object-anchored carve-out.
> **WHY:** LTO hands ld ltrans temporaries, every selector stops matching, and the pre-copy path would have linked and bricked before the `ASSERT`s existed.
> **REOPENS:** a measured LTO gate whose selectors still pin the six TUs.

## Vector Table Residency

Every `.vectors` entry except index 1 holds an SRAM address in
`[0x20000000, 0x20042000)`. Index 1 is `Reset_Handler` and stays in flash
`[0x10000000, 0x20000000)` by design: boot2 loads MSP/PC from the vectors LMA
at `0x10000100`.

`system/era_vector_defaults.c` supplies 38 `ERA_VECTOR_DEFAULT` aliases
(33 unconditional + five conditional PWM/PIO/DMA slots;
`era_board_adoption.md`'s **Copy-To-RAM Policy**) onto one
`era_unhandled_vector()`. How many a given image links is a board fact. No
ChibiOS or QMK edit; `vectors.o` stays pinned. `NMI_Handler` is absent from
the list because the ChibiOS RP2 port installs it.

Flash program/erase may run with core0 interrupts enabled because the image
and every vector except reset live in SRAM. HardFault and NMI are not
maskable by PRIMASK (`system/era_vector_defaults.c`); that is the
PRIMASK-to-NVIC trade — SRAM residency, not a PRIMASK hold, is the protection.

Two complementary checks: **Layout Checks** in `era_performance_gates.md`
reads the linked table's bytes; the linker `ASSERT` on `era_unhandled_vector`
catches the object going missing. Attribution, not recovery; no fault
recovery is planned.

> **REFUSED:** override `_unhandled_exception` alone as the vector-table fix.
> **WHY:** consecutive `VectorNNN:` labels collapse onto one flash `bl`, so a
> strong `_unhandled_exception` moves only that branch target and every table
> entry still points at the flash default.
> **REOPENS:** the table entries themselves no longer resolve to a collapsed
> flash default.

## Double-Tap Magic Placement

The magic is one 4-byte word that must sit outside every crt0 copy and clear
range. `magic_location` in `platforms/chibios/bootloaders/rp2040.c` is global
under `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM` so the pinned pre-copy
TU can write it.

| Constraint | Rule |
| --- | --- |
| section | `.era_bootloader_magic (NOLOAD)` immediately after `.bss` |
| before | the ChibiOS `*(.ram0.*)` sweep so that sweep loses the word |
| `ASSERT`s | size 4; base ≥ `__bss_end__`; `magic_location` == `__era_bootloader_magic_base__` |

**Layout Checks** independently compares the linked addresses. A changed
section attribute empties the region and fails the size `ASSERT`; a
disconnected writer fails the symbol/base `ASSERT`.

The dead zone was never measured and cannot be: no timebase exists before
`clocks_init()` in `platforms/chibios/bootloaders/rp2040.c`, and
`NVIC_SystemReset()` resets only the requesting core. 35–80 ms was arithmetic,
not a measurement. Tests are bounded to RUN-pin and cold boots.

> **REFUSED:** re-measure the pre-copy double-tap dead zone from firmware.
> **WHY:** no timebase exists before `clocks_init()`, and `NVIC_SystemReset()` resets only the requesting core.
> **REOPENS:** an external instrument that times RUN-pin and cold boots.

### Pre-copy reset guard layout

The production no-init exception is exactly the four-byte `magic_location`
region. Adding any other pre-copy SRAM object reopens the invariant.

`early_hardware_init_pre()` in `system/era_boot_core1_halt.c` may touch only
reset-domain MMIO, ROSC `COUNT`, the retained magic, and stack locals; no
call into `.sram_image`. The fixed successful guard is
`ERA_BOOT_GUARD_ROSC_CHUNK_COUNT` 768 × `ERA_BOOT_GUARD_ROSC_CHUNK_TICKS` 240
(`_Static_assert` == 184320 in `system/era_boot_core1_halt.c`). Each chunk has
`ERA_BOOT_GUARD_ROSC_POLL_LIMIT` 256. The PSM acknowledge wait is
`ERA_BOOT_CORE1_HALT_ACK_SPINS` 1000. It may write no oscillator
configuration, divider, frequency, XOSC, or clock-tree register. Watchdog
scratch0 is reset-domain MMIO, not a no-init allocation. Post-copy timeout
state lives in ordinary `.bss`; disarm in
`platforms/chibios/bootloaders/rp2040.c` clears both fields and the retained
magic.

## Core1 Vector Path

`era_split_communication_core_start()` in
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`
copies `SCB->VTOR` into its `ERA_SPLIT_COMMUNICATION_CORE_VECTOR_COUNT` 48
entry, 256-byte-aligned SRAM5 table and patches TIMER_IRQ3 only when
`launched` is clear (first launch, and after `declare_dead`). A wake of an
already-launched core sends `__SEV()` and does not re-send the vector address.

## Flash Write Guards And Boot Ordering

ERA NVM is the only production EEPROM flash writer. Void QMK write surface,
readback verify, and no keyboard/wire/scheduler work inside a primitive:
`era_host_peer_storage_contract.md`.

**Boot-ordering invariant, load-bearing and recorded here.** The boot ERA NVM
mount and the hardware-id unique-ID read both run before the first possible
core1 launch. **No earlier core1 launch entry point may be added.**
`eeprom_driver_init()` in `storage/era_eeprom_driver.c` mounts from
`keyboard_setup()` in `quantum/keyboard.c`.
`era_split_usb_identity_init()` in `split/era_split_usb_identity.c` reads the
unique ID from `era_split_keyboard_pre_init()` in `split/era_split_keyboard.c`
during that same `keyboard_setup()`. The first launch is
`era_split_keyboard_post_init()` in `split/era_split_keyboard.c`, the last
step of `keyboard_init()` in `quantum/keyboard.c` via `keyboard_post_init_kb()`
in `split/era_split_board.c`. Launch site and the policy/launch split:
`era_invariants.md`.

Two ELF facts: `__init_ram_areas()` does not clear `.bss` — crt0's own BSS
loop does; it zeroes `.ram5_clear` (`0x20041000..0x200418BF`, 2240 B). Named
in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`.

## Apply-Liveness Design

The durable Apply holds no core1 stop across the ERA NVM transaction. Core0
keepalive is forbidden: core0 is the actor the flash operation blocks. A
relation failure during the call does not change NVM authority. ADMIT,
old-or-new, repair-forward, both directions, and the 24-KiB public RAM image:
`era_host_peer_storage_contract.md`. Inactive-bank maintenance, mandatory
rotation, and the refusal of recursive keyboard/wire work from inside ERA NVM:
`era_host_peer_storage_contract.md` **Inactive-Bank Maintenance And Rotation**.

> **REFUSED:** a core0 keepalive across the ERA NVM transaction.
> **WHY:** core0 is the actor the flash operation blocks.
> **REOPENS:** a design where flash work does not run on core0.

## Closed Placement Surfaces

Do not reopen without a new active measured plan:

- any per-function RAM/flash placement macro layer (hot islands, RAMFUNC /
  not-in-flash wrappers, custom code sections) in ERA or QMK common code;
- non-striped main-SRAM bank partitioning or double-mapped bank aliases;
- XIP cache reclaim as SRAM (the recorded 16 KiB reserve above, unused);
- moving shared SPSC rings or publication words into a scratch bank (scratch
  banks stay core-private; shared records stay in main SRAM);
- QMK core matrix edits.

Migrating additional core1-private mutable state into the SRAM5 remainder is
not closed; it is evidence-gated on measured bank contention.

## Verification

`common/tools/era_residency_gate.sh` takes an ELF and nothing else.

| Check | Accept |
| --- | --- |
| residency | sum of ALLOC ram0 sections in `[0x20000000, 0x20040000)` excluding `.heap`; fail if the walk finds zero sections |
| floor | 32768 of 262144 B free |
| allocator | `malloc` / `calloc` / `realloc` / `_sbrk` / `chCoreAlloc` / `chCoreAllocI` / `chCoreAllocFromBaseI` / `chCoreAllocFromTopI` / `chHeapAlloc` undefined in the ELF |
| `.vectors` | 48 entries; index 1 flash `[0x10000000, 0x20000000)`; all others SRAM `[0x20000000, 0x20042000)` |
| input | ELF only — no board, no variant, no build |

One paced line in `split/diagnostics/era_split_wire_diagnostics.c` reports
`chCoreGetStatusX()` against `__heap_base__`..`__heap_end__` once per capture.
The standing procedure is **Layout Checks** in `era_performance_gates.md`.

> **REFUSED:** run the ram0 / allocator / `.vectors` checks from one board's launcher only.
> **WHY:** `newone/odessey60s` linked `malloc` through RGBLIGHT for ten days while the check lived inside one board's launcher.
> **REOPENS:** none — the gate takes an ELF and nothing else.
