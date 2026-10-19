# ERA Overview

Genre: contract
Canonical for: the ERA system mental model and shared vocabulary. Detailed
rules, numeric contracts, symbol behavior, and validation stay in their owning
contracts, source, and checks.

## Product Shape

RP2040 ERA boards use the common ERA firmware layer and execute from SRAM.
`sirind/brick65` is the permanent atmega32u4 runtime exception, not migration
debt. Split and non-split boards are both first-class product modes. Board
adoption and build identity are owned by `manuals/era_board_adoption.md` and
`manuals/era_build_options.md`.

Split boards share one serial relation between halves. Non-split boards have no
split-runtime dependency. The HID-production boundary and QMK-fork constraints
are owned by `contracts/era_invariants.md`.

## Execution Model

| Plane | Owner | First source anchor |
| --- | --- | --- |
| QMK-visible policy, live state, snapshots, result apply | core0 | `split/era_split_transport_scheduler.c` |
| split serial execution under an owner epoch | core1 | `split/communication_core/` |
| memory placement and copy-to-RAM lifetime | residency contract | `contracts/era_sram_residency_contract.md` |

The ownership boundary is normative only in `contracts/era_invariants.md`; this
table is the mental model used to find that contract.

## Relation Model

The scheduler classifies the split link into one relation. Relation ownership,
hold, initiator selection, and session authority are canonical in
`contracts/era_authority_contract.md`; route grants and timing are canonical in
`contracts/era_route_contract.md`.

| Relation | Meaning |
| --- | --- |
| **LOCAL_NO_LINK** | no confirmed peer; the half operates locally |
| **HOST-PEER** | exactly one half owns a usable USB host session; roles are HOST and PEER |
| **DUAL-HOST** | both halves independently own usable USB host sessions |

Wire eligibility is not inferred from this table. The exact section surface is
`contracts/era_wire_contract.md` and `contracts/era_closed_surface_contract.md`.

## State Boundaries

| Boundary | Contract owner |
| --- | --- |
| local USB facts -> relation authority | `contracts/era_authority_contract.md` |
| core0 policy/snapshot -> core1 execution -> result | `contracts/era_route_contract.md` and `contracts/era_invariants.md` |
| half <-> half packet and section content | `contracts/era_wire_contract.md` |
| durable cross-half replacement | `contracts/era_host_peer_storage_contract.md` |

The authority sample is the architecture's intentional poll. Other steady-state
cross-boundary work is driven by published state, deadlines, or admitted wire
traffic; exact cadence and the exception are owned by the authority and route
contracts.

## Shared Vocabulary

| Term | Meaning |
| --- | --- |
| **source-push** | the admitted initiator request carrier; HOST-PEER matrix publication is one use of it |
| **HSRSP** | `HOST_PEER_HOST_SOURCE_RSP`, the admitted response-slot envelope |
| **matrix_ready** | the no-host readiness fact required before HOST-PEER matrix admission |
| **replacement storage** | the shared multi-domain durable replacement engine used in admitted relations |
| **agreed restart** | a bilateral act committed at one shared-clock deadline; current users are link-speed and EEPROM CLEAN |
| **CORE1_FULL** | the only ERA split communication-core stage |

Packet names and bodies: `contracts/era_wire_contract.md`. Storage transaction
semantics: `contracts/era_host_peer_storage_contract.md`. Build-only measurement
vocabulary and variants: `manuals/era_build_options.md`.
