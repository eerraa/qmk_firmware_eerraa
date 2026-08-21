---
paths:
  - "keyboards/era/common/system/era_boot_core1_halt.c"
  - "keyboards/era/common/system/era_vector_defaults.c"
  - "keyboards/era/common/split/era_split_keyboard.*"
  - "keyboards/era/ld/**"
  - "platforms/chibios/bootloaders/rp2040.c"
  - "platforms/bootloader.h"
---

Boot path / pre-copy carve-out / load layout territory. Canonical read set
(Task Read Matrix, `era_active_index.md`): `contracts/era_invariants.md`,
`contracts/era_sram_residency_contract.md`, `manuals/era_performance_gates.md`,
`maps/era_source_map.md`, plus the carve-out rules written at the selector
list in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`. The launch step or
`is_keyboard_master()` adds `contracts/era_authority_contract.md`. Layout changes
run the manual's **Layout Checks** and require a fresh qwin comparison point.
