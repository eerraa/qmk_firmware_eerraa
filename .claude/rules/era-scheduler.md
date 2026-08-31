---
paths:
  - "keyboards/era/common/split/era_split_wire_router.*"
  - "keyboards/era/common/split/era_split_transport_scheduler.*"
  - "keyboards/era/common/split/scheduler/**"
  - "keyboards/era/common/split/era_split_scheduler_events.h"
---

Scheduler hot path / dirty-due / route execution territory. Canonical read set
(Task Read Matrix, `era_active_index.md`): `contracts/era_route_contract.md`,
`manuals/era_performance_gates.md`, `maps/era_source_map.md`. Keep hot-path
decisions O(1); no allocation, broad scans, CRC, or snapshot construction on
scan-bound router paths (`AGENTS.md`, Change Rules).
