# ERA Overview

Genre: contract
Canonical for: the ERA mental model and shared vocabulary. The plain-language
architecture picture and the glossary live only here; every rule, ownership
row, and symbol name stays canonical in the documents this file points to.

## What ERA Is

Every board under `keyboards/era` is an ERA board. Twenty-two RP2040 boards
take the whole ERA firmware layer; `sirind/brick65` (atmega32u4) takes none
of that firmware — a permanent runtime exception, not a debt. All twenty-three
share one automated-build identity under `keyboards/era/common/`. Every RP2040
board runs the image from SRAM
(`manuals/era_board_adoption.md`'s **Copy-To-RAM Policy**).

Three boards are split — `sirind/tomak`, `sirind/tomak79h`, `sirind/tomak79s`
— and talk over one serial wire. Non-split boards are first-class
(`manuals/era_board_adoption.md`'s **Non-Split Board Baseline**). On every
board, QMK's matrix/debounce path is the sole HID producer
(`contracts/era_invariants.md`).

## The Two Cores

Two cores. Core0 publishes immutable requests and applies generation-matched
results. Core1 is the wire executor: every serial RX/TX under an owner epoch.
Core1 never reads live QMK state and never writes EEPROM. Placement does not
move that split (`contracts/era_sram_residency_contract.md`'s **Ownership Does Not Move With Placement**). `CORE1_FULL` is the only stage (`contracts/era_invariants.md`).

## The Relation Model

The scheduler classifies the split link into a relation
(`contracts/era_authority_contract.md`; mode identifiers:
`maps/era_identifier_map.md`). Routes follow that classification
(`contracts/era_route_contract.md`).

| Relation | Meaning |
| --- | --- |
| **LOCAL_NO_LINK** | no confirmed peer; the half runs standalone |
| **HOST-PEER** | one half is USB-enumerated (**HOST**); the other is not (**PEER**) |
| **DUAL-HOST** | both halves independently USB-enumerated, normally two ports on the same computer. Both-hosted is always DUAL-HOST |

Initiator, responder, and which routes may open are projections of that
relation (`contracts/era_authority_contract.md`, `contracts/era_invariants.md`).

## How State Crosses

Three boundaries, one mechanism each:

| Boundary | What crosses | Where it is canonical |
| --- | --- | --- |
| core0 ↔ its own USB hardware | local authority facts | `contracts/era_authority_contract.md` |
| core0 ↔ core1 | published plans and results | `contracts/era_route_contract.md` |
| half ↔ half | wire sections, per relation | `contracts/era_wire_contract.md` |

The side that changes publishes; the side that consumes wakes on an edge.
Cost scales with how often the fact changes, not with how often anyone looks.

**Exactly one thing polls**, and it is named rather than tolerated: the
authority sample of the USB SOF counter. Everything else is an edge. The
sample itself: `contracts/era_authority_contract.md`. Both serviced relations
run core1's standing exchange as their steady state
(`contracts/era_route_contract.md`).

## Glossary

| Term | Meaning |
| --- | --- |
| **HOST / PEER** | the two roles of a HOST-PEER relation (above) |
| **DUAL-HOST** | the both-hosted relation (above) |
| **source-push** | two uses of one name: the HOST-PEER matrix lane, where the PEER publishes latest local rows so the HOST projects both halves into one HID report (`contracts/era_host_peer_matrix_contract.md`); and the wire operation `HOST_PEER_SOURCE_PUSH`, which also carries the initiator's runtime sections in both relations (`contracts/era_wire_contract.md`) |
| **HSRSP** | the admitted-slot response envelope `HOST_PEER_HOST_SOURCE_RSP`. Both relations return response sections on it; which sections: `contracts/era_closed_surface_contract.md` |
| **matrix_ready** | the no-host local-matrix-ready bit on `SESSION_STATUS` and AUTHORITY. HOST-PEER matrix admission requires it (`contracts/era_authority_contract.md`'s **Matrix Ready**) |
| **replacement storage** | one seven-domain EEPROM replacement engine in both admitted relations. The relation's initiator selects; arbitration picks direction; the apply target is the half that receives the candidate. Probe, then chunks, then durable apply (`contracts/era_host_peer_storage_contract.md`) |
| **audit sweep** | the mandatory seven-domain verify-all probe at every serviced relation (re)establishment (`contracts/era_host_peer_storage_contract.md`) |
| **storage news value** | the responder's forward-only 7-bit hint (`0` = no claim). It names no domain; a new nonzero value tells the initiator to run a summary (`contracts/era_host_peer_storage_contract.md`'s **Storage News Value And Relation-Open Audit**) |
| **durable apply** | after ADMIT, one synchronous ERA NVM replacement of a validated staged image. Public EEPROM stays old until that commit; core1 keeps the relation alive from SRAM (`contracts/era_host_peer_storage_contract.md`) |
| **responder-silence stale watch** | the responder's watch on the core1 accepted-RX counter; silence forgets the session. Period: `contracts/era_route_contract.md`'s **Stale Recovery** |
| **promotion** | a former PEER whose session owner is now local — this half decides from its own USB session, on the last durable image it already holds |
| **agreed restart** | both halves commit the same act at one shared-clock deadline. The mechanism knows neither user. Link-speed and EEPROM CLEAN are the two (`split/era_split_restart_agreement.h`; wire in `contracts/era_wire_contract.md`; what the cell is open for in `contracts/era_closed_surface_contract.md`) |
| **reconciliation** | the pair meets at Low, then raises to the winner's stored level (`split/era_split_link.h`; why it is a route-layer fact in `contracts/era_route_contract.md`) |
| **cold task boundary** | the housekeeping cadence, not matrix scan, where storage capture, CRC, and apply run |
| **CORE1_FULL** | the only communication-core stage: core1 owns all wire IO (`contracts/era_invariants.md`) |
| **qwin** | the no-cable standalone scan-rate benchmark window (`WIRE_QWIN`). Floor: `manuals/era_performance_gates.md` |
| **rung** | a build variant that exists only to attribute a measurement: the all-on image with exactly one mechanism turned off, so only the difference against that sitting's all-on window counts. It is not a profile: launcher artifact of its own, outside the default sweep, never a comparison point. Four have retired (`qwin_piooff`, `qwin_wfeoff`, `qwin_gateoff`, `qwin_limit20`). `qwin_phase` is the standing exception: it turns an instrument on, because the instrument costs scan rate. Live variant identity: `manuals/era_build_options.md` |
