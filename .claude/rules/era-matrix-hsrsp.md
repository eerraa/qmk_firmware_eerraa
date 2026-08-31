---
paths:
  - "keyboards/era/common/split/era_host_peer_matrix_link.*"
  - "keyboards/era/common/split/era_host_peer_transaction.h"
  - "keyboards/era/common/split/era_host_peer_responder.c"
  - "keyboards/era/common/split/era_host_peer_response.c"
  - "keyboards/era/common/split/era_host_peer_source_snapshot.*"
  - "keyboards/era/common/system/era_matrix_*"
  - "keyboards/era/common/system/era_rp2040_matrix*"
---

HOST-PEER matrix source-push / HSRSP / matrix-engine territory. Canonical read
set (Task Read Matrix, `era_active_index.md`):
`contracts/era_host_peer_matrix_contract.md`, `contracts/era_route_contract.md`,
`maps/era_source_map.md`. RGB / visual-sync / lock / indicator / time-anchor
work is a different row and starts elsewhere: `contracts/era_wire_contract.md`
**first**, because each of those is a response section and that contract holds
the marker, the body layout and the per-relation eligibility; then
`contracts/era_authority_contract.md` for the policy gate and
`contracts/era_host_peer_matrix_contract.md`, with
`manuals/era_capture_reading.md` for the counters.
