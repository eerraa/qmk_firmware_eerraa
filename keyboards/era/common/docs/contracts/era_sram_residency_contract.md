# ERA SRAM-Resident Load Image Contract

Genre: contract
Canonical for: ERA RP2040 memory placement and the load layout, copy-to-RAM
execution scope, the flash startup carve-out, the 32 KiB ram0 floor, the
boot-ordering invariant that binds the vector table to the core1 launch
step, and the closed placement surfaces

Ownership lists: `era_invariants.md`, `era_authority_contract.md`. Copy-to-RAM
policy: `era_board_adoption.md`'s **Copy-To-RAM Policy**. Boot-safety cluster:
`era_invariants.md`. Layout procedure: `era_performance_gates.md`'s **Layout
Checks**. Apply, ERA NVM, and inactive-bank rotation:
`era_host_peer_storage_contract.md`.

## Why The Image Is Resident

QSPI NOR cannot serve instruction fetches during program/erase. Every ERA
RP2040 board (`sirind/brick65` excepted) executes the whole image from SRAM
on both cores. Placement grants no new access.

## Load Layout

Script: `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`. Selected as
`MCU_LDSCRIPT` by `system/era_sram_resident_rules.mk`. Split boards take that
include from `split/era_split_qmk_rules.mk`; non-split boards include it from
`post_rules.mk` after `system/era_common_qmk_rules.mk`. One finished layout.
No legacy XIP path. RP2040-only.

Flash keeps `.boot2` at `0x10000000`, the `.vectors` LMA at exactly
`0x10000100` (boot2 fetches reset MSP/PC from that address), the
`.flash_startup` carve-out, the `.sram_image` LMA, and the reserved final
128 KiB ERA NVM (`__era_nvm_region_base__` / `__era_nvm_region_end__` in that
linker script). Default `flash1` length is 2048 KiB.

| Region | Address / size | Holds |
| --- | --- | --- |
| `ram0` | `0x20000000`, 256 KiB striped (262144 B) | `.vectors` VMA, `.sram_image`, `.bss`, `.era_bootloader_magic`, elastic `.heap` |
| `ram4` SRAM4 | `0x20040000`, 4 KiB | core0 process stack `__process_stack_size__` 0x800 plus exception stack `__main_stack_size__` 0x400 (`platforms/chibios/platform.mk` defaults; ERA does not override) |
| `ram5` SRAM5 | `0x20041000`, 4 KiB | core1-private `.ram5_clear`: stack `ERA_SPLIT_COMMUNICATION_CORE_STACK_WORDS` 512 (2048 B) and 256-byte-aligned vector table `ERA_SPLIT_COMMUNICATION_CORE_VECTOR_COUNT` 48 (192 B) in `split/communication_core/era_split_communication_core_lifecycle_rp2040.c`. Occupied span 2240 B at `0x20041000..0x200418BF` |
| `ram7` | `0x20041f00`, 256 B | ROM scribble during reset |
| unclaimed | 1600 B | `0x200418C0..0x20041EFF`, below `ram7` |

Scratch banks are per-core private. Core1 stack and vector fetches never
contend with core0 in striped `ram0`. Shared SPSC rings and publication words
stay in main SRAM.

The 32 KiB ram0 floor is 32768 bytes free of 262144. `tools/era_residency_gate.sh`
enforces it on the ELF (ALLOC ram0 sections excluding `.heap`). Do not restate
that script here.

## Bundle

`system/era_sram_resident_rules.mk` is one include, not a variable. It sets
`MCU_LDSCRIPT`, `-DERA_SRAM_RESIDENT_IMAGE`, an unconditional linker map,
`RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM`, and `SRC` for
`system/era_boot_core1_halt.c`, `system/era_vector_defaults.c`,
`storage/era_nvm.c`, and `storage/era_nvm_rp2040.c`. `era_nvm_rp2040.c` and
`era_vector_defaults.c` `#error` without `ERA_SRAM_RESIDENT_IMAGE`.

Two `$(error)` gates fire before any compile: the bundle requires
`ERA_COMMON_QMK_RULES_INCLUDED` (from `system/era_common_qmk_rules.mk`) and
`ERA_BOARD_COMMON_ENABLE=yes` (class skeleton owns `housekeeping_task_kb`).

> **REFUSED:** the copy-to-RAM bundle without `era_common_qmk_rules.mk`, or with
> `ERA_BOARD_COMMON_ENABLE=no`.
> **WHY:** the bundle arms the double-tap window before crt0 copies SRAM; the
> only closer is `era_common_features.c` via the class skeleton, so an image
> with the arm and no closer enters BOOTSEL on every physical RUN.
> **REOPENS:** a closer named in the same bundle, with a compile-time refusal
> of the arm-without-closer combination.

Keep the default `BOARD`, or amend the `.flash_startup` path-anchored
`RP_PICO_RP2040/board.o` selector in the same change.

> **REFUSED:** a bare `*board.o` suffix as the carve-out selector.
> **WHY:** that suffix matches five objects in a split build (and five in a
> non-split build), so it pins the wrong translation units into flash.
> **REOPENS:** a selector that matches only the RP Pico board object.

## Flash Startup Carve-Out

Six pinned TUs in `.flash_startup`. Selectors and the three addition rules
(silent flash→SRAM veneer, mandatory `.rodata` arm, `Reset_Handler`→`_crt0_entry`
order) live at the selector list in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`.
The `ASSERT`s catch a dropped `BOARD` or `SRC`. Closure itself is not caught.
Objdump/nm review: `era_performance_gates.md` **Layout Checks**.

| TU | Pin |
| --- | --- |
| `crt0_v6m.o` | `.text` |
| `vectors.o` | `.text`; adjacent to crt0 |
| `crt1.o` | `.text` + `.rodata` |
| path-anchored `RP_PICO_RP2040/board.o` | `.text` + `.rodata` |
| `chibios.o` (`tmk_core/protocol/chibios`) | `__early_init`, `early_hardware_init_pre`, `early_hardware_init_post` only |
| `system/era_boot_core1_halt.o` | last; `.rodata` arm; tighter `ASSERT` |

`early_hardware_init_pre()` in `system/era_boot_core1_halt.c` is the whole
pre-copy window.

| Symbol | Must link inside |
| --- | --- |
| `Reset_Handler`, `_crt0_entry`, `__cpu_init`, `__early_init`, `__chibios_override___early_init`, `early_hardware_init_post` | `__flash_startup_base__` / `__flash_startup_end__` |
| `early_hardware_init_pre` | `__era_boot_core1_halt_base__` / `__era_boot_core1_halt_end__` |
| `era_unhandled_vector` | SRAM `.text` |

Necessary, not a closure proof. Two expected 16-byte stubs in `.flash_startup`
serve the post-copy `main` / `__late_init` calls (`__main_veneer`,
`____late_init_veneer`). A third veneer for a pre-copy caller is the bug
signature. Pinning an existing TU costs negative SRAM; pinning a flash-only TU
costs zero.

Stock crt0 copies `.sram_image` (ram0 VMA, flash LMA) through `CRT0_INIT_DATA`
via redefined `__textdata_base__` / `__data_base__` / `__data_end__`, and copies
`.vectors` through `CRT0_INIT_VECTORS`, which then retargets VTOR. The ERA
script INCLUDEs the stock ChibiOS stack and memory rule files and does not
INCLUDE the c1 stack reservation, consistent with `CRT0_EXTRA_CORES_NUMBER=0`
in `platforms/chibios/vendors/RP/RP2040.mk`: ERA launches core1 itself. crt0
clears `.bss`; `.ram5_clear` is zeroed through the ram_areas table those
INCLUDEs fill.

Accepted residual: `CRT0_VTOR_INIT=1` in that same `RP2040.mk` points VTOR at
the not-yet-copied SRAM table for the few hundred pre-copy cycles.

> **REFUSED:** LTO on this image.
> **WHY:** LTO hands ld ltrans objects instead of the names these selectors
> match, so every object-anchored `ASSERT` fails together.
> **REOPENS:** a measured LTO gate whose selectors match the objects LTO
> actually emits.

## Vector Table Residency

Every `.vectors` entry except index 1 holds an SRAM address. Index 1 is
`Reset_Handler` and stays in flash: boot2 loads MSP/PC from the vectors LMA at
`0x10000100`. `tools/era_residency_gate.sh` reads the linked table's 48 bytes;
index 1 must be flash, every other entry SRAM. The linker `ASSERT` on
`era_unhandled_vector` catches a dropped `SRC` only.

`system/era_vector_defaults.c` supplies `ERA_VECTOR_DEFAULT` aliases onto one
`era_unhandled_vector` in SRAM `.text`. 33 are unconditional. Five are
conditional on the installing driver: `Vector50` (PWM), `Vector5C` / `Vector64`
(PIO0_IRQ_0 / PIO1_IRQ_0), `Vector6C` / `Vector70` (DMA). How many a given
image links is a board fact. Conditions: `era_board_adoption.md`'s **Copy-To-RAM
Policy**. `CORTEX_NUM_VECTORS` must be 32. No ChibiOS or QMK edit; `vectors.o`
stays pinned.

Flash program/erase may run with core0 interrupts enabled because the image and
every vector except reset live in SRAM. HardFault and NMI are not maskable by
PRIMASK (`system/era_vector_defaults.c`); SRAM residency, not a PRIMASK hold,
is the protection. Attribution, not recovery; no fault recovery is planned.

> **REFUSED:** override `_unhandled_exception` alone as the vector-table fix.
> **WHY:** consecutive `VectorNNN:` labels collapse onto one flash `bl`, so a
> strong `_unhandled_exception` moves only that branch target and every table
> entry still points at the flash default.
> **REOPENS:** the table entries themselves no longer resolve to a collapsed
> flash default.

## Double-Tap Magic And Pre-Copy Window

The magic is one 4-byte word that must sit outside every crt0 copy and clear
range. `magic_location` in `platforms/chibios/bootloaders/rp2040.c` is global
under `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM` so the pinned pre-copy
TU can write it.

| Constraint | Rule |
| --- | --- |
| section | `.era_bootloader_magic (NOLOAD)` immediately after `.bss` in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` |
| before | the stock memory INCLUDE, so the ChibiOS ram0 `*(.ram0.*)` sweep loses the word |
| `ASSERT`s | size 4; base ≥ `__bss_end__`; `magic_location` == `__era_bootloader_magic_base__` |

A changed section attribute empties the region and fails the size `ASSERT`; a
disconnected writer fails the symbol/base `ASSERT`. The production no-init
exception is exactly that four-byte region.

`early_hardware_init_pre()` in `system/era_boot_core1_halt.c` classifies the
double-tap first, then hardware-halts core1. It may touch only reset-domain
MMIO, ROSC `COUNT`, the retained magic, and stack locals; no call into
`.sram_image`. It may write no oscillator configuration, divider, frequency,
XOSC, or clock-tree register. Watchdog scratch0 is reset-domain MMIO, not a
no-init allocation. Post-copy timeout state lives in ordinary `.bss`; disarm
in that bootloader file clears both fields and the retained magic.

The window is non-blocking. Only an eligible physical RUN may arm. The
stable-release guard is `ERA_BOOT_GUARD_ROSC_CHUNK_COUNT` 768 ×
`ERA_BOOT_GUARD_ROSC_CHUNK_TICKS` 240 (`_Static_assert` == 184320 in
`system/era_boot_core1_halt.c`); each chunk has `ERA_BOOT_GUARD_ROSC_POLL_LIMIT`
256. Datasheet 1.8–12 MHz → about 15.36–102.4 ms. The halt acknowledge wait is
`ERA_BOOT_CORE1_HALT_ACK_SPINS` 1000. The halt sets PSM `FRCE_OFF PROC1`, waits
that bound, and clears `FRCE_OFF` before return. Tap counting vs bootrom jump
split, and the three magic states: `era_invariants.md`.

> **REFUSED:** re-measure the pre-copy dead zone in milliseconds.
> **WHY:** no timebase exists before `clocks_init()`, and `NVIC_SystemReset()`
> resets only the requesting core.
> **REOPENS:** a timebase that is valid in the pre-copy window.
>
> Tests of the window are bounded to RUN-pin and cold boots.

## Boot Order

Load-bearing. No earlier core1 launch entry point may be added. Launch site and
the policy/launch split: `era_invariants.md`.

| Step | Where |
| --- | --- |
| Pre-copy: double-tap classify, then core1 halt | `early_hardware_init_pre()` in `system/era_boot_core1_halt.c` |
| crt0 copies `.vectors` and `.sram_image`, clears `.bss`, zeroes `.ram5_clear` | stock crt0; layout in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` |
| ERA NVM mount (including a fresh-bank format when no valid bank exists) | `eeprom_driver_init()` in `storage/era_eeprom_driver.c`, from `keyboard_setup()` in `quantum/keyboard.c` / `quantum/main.c`, calls `era_nvm_mount()` in `storage/era_nvm.c` |
| Hardware-id unique-ID read (split) | `get_hardware_id()` from `era_split_keyboard_pre_init()` in `split/era_split_keyboard.c` / `split/era_split_usb_identity.c`, still inside `keyboard_setup()` |
| USB attach | `protocol_pre_init()` in `tmk_core/protocol/chibios/chibios.c` |
| First core1 launch | last step of `keyboard_init()` in `quantum/keyboard.c`: `era_split_keyboard_post_init()` in `split/era_split_keyboard.c` runs `era_host_peer_storage_init()` then `era_split_transport_scheduler_start_communication_core()` in `split/era_split_transport_scheduler.c`. Not a call after `keyboard_init()` returns. Non-split boards still halt core1 (idempotent) and do not launch it. |

`era_split_communication_core_launch_sequence()` in
`split/communication_core/era_split_communication_core_lifecycle_rp2040.c`
copies `SCB->VTOR` into the ram5 table and patches TIMER_IRQ3. That copy runs
on first launch and on a relaunch after `era_split_communication_core_declare_dead()`
in the same file. A `start()` of an already-launched core only `__SEV()`s.

The vector table and the launch step are one fact: flash program/erase with
core0 interrupts enabled requires SRAM `.vectors`; core1's first launch copies
that SRAM table into ram5.

## Scan Path And Apply

Placement relaxes none of the scan-path or core1-epoch rules. File ownership:
`era_source_map.md`. Storage capture, EEPROM, CRC, comparison, route enqueue,
result drain, replacement Apply, runtime reload, and diagnostics execute only
at task/housekeeping boundaries, never inside matrix scan. Matrix-scan transport
reads cached scalar facts only. Role lifecycle, send, response-window RX, and
responder-idle RX execute only under the core1 owner epoch.

The durable Apply holds no core1 stop across the ERA NVM transaction. Core0
keepalive is forbidden: core0 is the actor the flash operation blocks. SRAM
residency is what lets core1 keep running. ADMIT and the 24-KiB public RAM
image: `era_host_peer_storage_contract.md`.

> **REFUSED:** stop core1, or run a core0 keepalive, across an ERA NVM
> program/erase.
> **WHY:** core0 is blocked in the flash primitive; a keepalive on the blocked
> core cannot beat a stall, and a core1 halt drops the standing liveness beat.
> **REOPENS:** a flash path that does not block core0.

## Closed Placement Surfaces

Migrating additional core1-private mutable state into the SRAM5 remainder is
not closed; it is evidence-gated on measured bank contention.

> **REFUSED:** a per-function RAM/flash placement macro layer (hot islands,
> RAMFUNC / not-in-flash wrappers, custom code sections) in ERA or QMK common.
> **WHY:** the load layout already places the whole image; a second placement
> machinery splits the carve-out without a closure proof.
> **REOPENS:** a measured plan that names the new selectors, ASSERTs, and
> veneer map.

> **REFUSED:** non-striped main-SRAM bank partitioning, or double-mapped bank
> aliases; XIP cache reclaim as SRAM (16 KiB unused reserve).
> **WHY:** striped `ram0` is the accepted contention model; reclaiming the XIP
> cache as SRAM has no measured gate.
> **REOPENS:** a measured bank map that keeps core1-private fetches out of
> striped `ram0`, or a gate that proves the 16 KiB reclaim.

> **REFUSED:** moving shared SPSC rings or publication words into a scratch bank.
> **WHY:** scratch banks stay core-private; a shared record in SRAM4 or SRAM5
> is a silent cross-core alias.
> **REOPENS:** a private-bank map that does not place a shared word in scratch.

## Verification

Instrument: `tools/era_residency_gate.sh`. ELF in; no board, no variant, no
build. Three enforced checks: ram0 free ≥ 32768 of 262144 excluding `.heap`;
allocator symbols named in that script (`malloc`, `calloc`, `realloc`, `_sbrk`,
`chCoreAlloc*`, `chHeapAlloc`) undefined; `.vectors` 48 entries, index 1 flash,
all others SRAM. ELF-only is load-bearing: `newone/odessey60s` linked `malloc`
via RGBLIGHT while the check lived inside one board's launcher.

One paced line in `split/diagnostics/era_split_wire_diagnostics.c` reports
`chCoreGetStatusX()` against `__heap_base__`..`__heap_end__` once per capture.
Standing procedure: **Layout Checks** in `era_performance_gates.md`.
