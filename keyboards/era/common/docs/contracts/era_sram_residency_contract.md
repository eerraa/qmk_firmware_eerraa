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
| production release vs wire | ~24 KiB more headroom | wire carries the diagnostics worst case |
| XIP cache reclaim | 16 KiB | unused; recorded reserve |
| LTO | off; expected 5–15% `.text` | needs its own measured gate before adoption |

The standing free-RAM requirement is the 32 KiB floor under **Verification**.

## Load Layout

The whole firmware image executes from SRAM on both cores (copy-to-RAM).
Scratch banks are per-core private: core1 stack and vector fetches never
contend with core0 in striped `ram0`.

| Region | Address / size | Holds |
| --- | --- | --- |
| flash (kept) | 2 MiB effective; 128-KiB NVM at the end | `.boot2` `0x10000000`; `.vectors` LMA `0x10000100`; flash startup carve-out; `.sram_image` LMA; ERA NVM region |
| `ram0` | 256 KiB striped (262144 B) | vector table, copied image, `.bss`, elastic heap; ≥32 KiB / 32768 free |
| `ram4` SRAM4 | `0x20040000`, 4 KiB | core0 main/process stacks 3 KB + 288 B ChibiOS core0 structures |
| `ram5` SRAM5 | `0x20041000`, 4 KiB | core1 stack 2048 B + 48×4 = 192 B vector table through `.ram5_clear` = 2240 B at `0x20041000..0x200418BF` |
| `ram7` | `0x20041f00`, 256 B | ROM scribble during reset |
| unclaimed | ~1.6 KiB | below `ram7` |

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
`MCU_LDSCRIPT` in `system/era_sram_resident_rules.mk`, included from every
RP2040 board `post_rules.mk`. One finished layout, no legacy XIP path, no
QMK-common / ChibiOS / pico-sdk file changes. RP2040-only.

Flash keeps only `.boot2` at `0x10000000`, the `.vectors` LMA at exactly
`0x10000100` (boot2 hardcodes the reset MSP/PC fetch), the flash startup
carve-out, the `.sram_image` LMA, and the 128-KiB ERA NVM region.

Six pinned TUs. A bare `*board.o` suffix is refused — it matches five objects
in this split build. Selectors and the three addition rules (silent flash→SRAM
veneer, mandatory `.rodata` arm, `Reset_Handler`→`_crt0_entry` order) live at
the selector list in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`. Keep the
default `BOARD`, or amend the carve-out in the same change.

| TU | Pin |
| --- | --- |
| `crt0_v6m.o` | `.text` |
| `vectors.o` | `.text`; adjacent to crt0 |
| `crt1.o` | `.text` + `.rodata` |
| path-anchored `RP_PICO_RP2040/board.o` | `.text` + `.rodata` |
| `chibios.o` | `__early_init`, `early_hardware_init_pre`, `early_hardware_init_post` only (`tmk_core/protocol/chibios`) |
| `system/era_boot_core1_halt.o` | last; `.rodata` arm; tighter ASSERT |

`early_hardware_init_pre()` in `system/era_boot_core1_halt.c` is the whole
pre-copy window. The `ASSERT`s catch a dropped `BOARD` or `SRC`; **closure
itself is not caught**. The objdump/nm review is `era_performance_gates.md`
**Layout Checks**.

The veneer set, not a byte count, is the gate: `__main_veneer` and
`____late_init_veneer` serve the post-copy calls at `crt0_v6m.S:289`/`:306`.
A third veneer is the pre-copy-caller signature. Pinning an existing TU costs
negative SRAM; pinning a flash-only TU costs zero.

| Symbol | Must link inside |
| --- | --- |
| `Reset_Handler`, `_crt0_entry`, `__cpu_init`, `__early_init`, `__chibios_override___early_init`, `early_hardware_init_post` | `__flash_startup_base__` / `__flash_startup_end__` |
| `early_hardware_init_pre` | `__era_boot_core1_halt_base__` / `__era_boot_core1_halt_end__` |
| `era_unhandled_vector` | SRAM `.text` |

Necessary, not a closure proof. Validated by injecting a removed `chibios.o`
selector, a removed `SRC` line, and a mismatched carve-out selector.

Everything else lands in one `.sram_image` (ram0 VMA, flash LMA), copied by
stock crt0 `CRT0_INIT_DATA` via redefined
`__textdata_base__`/`__data_base__`/`__data_end__`; `.vectors` copies through
`CRT0_INIT_VECTORS`, which then retargets VTOR. Stock `rules_stacks.ld` and
`rules_memory.ld` stay INCLUDEd. `rules_stacks_c1.ld` is not included — the
c1 stack reservation is dropped, consistent with `CRT0_EXTRA_CORES_NUMBER=0`,
because ERA launches core1 itself.

Accepted residual: `CRT0_VTOR_INIT=1` points VTOR at the not-yet-copied SRAM
table for the few hundred pre-copy cycles. Accepted to keep the vendor
makefiles untouched.

## Vector Table Residency

Every `.vectors` entry except index 1 holds an SRAM address. Index 1 is
`Reset_Handler` and stays in flash by design: boot2 loads MSP/PC from the
vectors LMA at `0x10000100`.

`system/era_vector_defaults.c` supplies 38 `ERA_VECTOR_DEFAULT` aliases
(33 unconditional + five conditional PWM/PIO/DMA slots;
`era_board_adoption.md`'s **Copy-To-RAM Policy**) onto one
`era_unhandled_vector`. How many a given image links is a board fact.
Re-derive: `grep -c '^ERA_VECTOR_DEFAULT(' keyboards/era/common/system/era_vector_defaults.c`.
Cost: 16 B of ram0. No ChibiOS or QMK edit; `vectors.o` stays pinned.

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
range.

| Constraint | Rule |
| --- | --- |
| section | `.era_bootloader_magic (NOLOAD)` immediately after `.bss` |
| before | `INCLUDE rules_memory.ld` so the ChibiOS `*(.ram0.*)` sweep loses the word |
| `ASSERT`s | size 4; base ≥ `__bss_end__`; `magic_location` == `__era_bootloader_magic_base__` |
| symbol | `magic_location` in `platforms/chibios/bootloaders/rp2040.c` is global so the pinned pre-copy TU can write it |

**Layout Checks** independently compares the linked addresses. A changed
section attribute empties the region and fails the size `ASSERT`; a
disconnected writer fails the symbol/base `ASSERT`.

The dead zone was never measured and cannot be: no timebase exists before
`clocks_init()`, and `NVIC_SystemReset()` resets only the requesting core.
Do not re-measure it. 35–80 ms was arithmetic, not a measurement. Tests are
bounded to RUN-pin and cold boots.

### Pre-copy reset guard layout

The production no-init exception is exactly the four-byte `magic_location`
region. Adding any other pre-copy SRAM object reopens the invariant.

`early_hardware_init_pre()` in `system/era_boot_core1_halt.c` may touch only
reset-domain MMIO, ROSC `COUNT`, the retained magic, and stack locals; no
call into `.sram_image`. The fixed successful guard is
`ERA_BOOT_GUARD_ROSC_CHUNK_COUNT` 768 × `ERA_BOOT_GUARD_ROSC_CHUNK_TICKS` 240
(`_Static_assert` == 184320 in `system/era_boot_core1_halt.c`). It may write
no oscillator configuration, divider, frequency, XOSC, or clock-tree
register. Watchdog scratch0 is reset-domain MMIO, not a no-init allocation.
Post-copy timeout state lives in ordinary `.bss`; disarm clears both fields
and the retained magic.

## Core1 Vector Path

`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`
copies `SCB->VTOR` into its `ERA_SPLIT_COMMUNICATION_CORE_VECTOR_COUNT` 48
entry, 256-byte-aligned SRAM table at first launch and patches TIMER_IRQ3;
restarts never re-send the vector address.

## Flash Write Guards And Boot Ordering

ERA NVM is the only production EEPROM flash writer. Void QMK write surface,
readback verify, and no keyboard/wire/scheduler work inside a primitive:
`era_host_peer_storage_contract.md`.

**Boot-ordering invariant, load-bearing and recorded here.** The boot ERA NVM
mount (including a fresh-bank format when no valid bank exists) and the
hardware-id unique-ID read both run before the first possible core1 launch.
**No earlier core1 launch entry point may be added.** Launch site and the
policy/launch split: `era_invariants.md`. The launch is later than
`keyboard_init()` (`quantum/main.c:44`).

Two ELF facts: `__init_ram_areas()` does not zero `.bss` — crt0's own loop
does (`crt0_v6m.S:267-281`); it zeroes `.ram5` (`0x20041000..0x200418BF`,
2240 B). Named in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`.

## Apply-Liveness Design

The durable Apply holds no core1 stop across the ERA NVM transaction. Core0
keepalive is forbidden: core0 is the actor the flash operation blocks. A
relation failure during the call does not change NVM authority. ADMIT,
old-or-new, repair-forward, both directions, and the 24-KiB public RAM image:
`era_host_peer_storage_contract.md`. Inactive-bank maintenance, mandatory
rotation, and the refusal of recursive keyboard/wire work from inside ERA NVM:
`era_host_peer_storage_contract.md` **Inactive-Bank Maintenance And Rotation**.

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

| Check | Accept |
| --- | --- |
| residency | sum of ALLOC ram0 sections excluding `.heap`; `common/tools/era_residency_gate.sh` |
| floor | 32768 of 262144 B free |
| allocator | `malloc` / `_sbrk` / `chCoreAlloc*` / `chHeapAlloc` undefined in the ELF |
| `.vectors` | 48 entries; index 1 flash; all others SRAM |
| input | ELF only — no board, no variant, no build |

One paced line in `split/diagnostics/era_split_wire_diagnostics.c` reports
`chCoreGetStatusX()` against `__heap_base__..__heap_end__` once per capture.
The standing procedure is **Layout Checks** in `era_performance_gates.md`.
ELF-only is load-bearing: `newone/odessey60s` linked `malloc` via RGBLIGHT
for ten days while the check lived inside one board's launcher.
