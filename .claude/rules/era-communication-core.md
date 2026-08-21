---
paths:
  - "keyboards/era/common/split/communication_core/**"
---

Core1 communication-core territory. Canonical read set (Task Read Matrix,
`era_active_index.md`): `manuals/era_performance_gates.md`,
`maps/era_source_map.md`, `contracts/era_authority_contract.md`,
`contracts/era_wire_contract.md`, `contracts/era_route_contract.md`,
`contracts/era_sram_residency_contract.md`. Core ownership is invariant territory:
core1 has no live QMK/EEPROM access and no ChibiOS wait APIs
(`contracts/era_invariants.md`).
