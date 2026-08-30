# ERA Overview

Genre: contract
Canonical for: the ERA mental model and shared vocabulary. The plain-language
architecture picture and the glossary live only here; every rule, ownership
row, and symbol name stays canonical in the documents this file points to.

Copy-to-RAM and the non-split baseline: `era_board_adoption.md`. HID producer,
`CORE1_FULL`, and traffic shape: `era_invariants.md`. Relation, session, and
the named poll: `era_authority_contract.md`. Standing exchange, grants, and
cadences: `era_route_contract.md`. Markers and bodies: `era_wire_contract.md`.
Open and closed cells: `era_closed_surface_contract.md`. SRAM placement:
`era_sram_residency_contract.md`. Storage and NVM apply:
`era_host_peer_storage_contract.md`. HOST-PEER matrix source-push:
`era_host_peer_matrix_contract.md`. Per-file owners: `era_source_map.md`.
Mode identifiers: `era_identifier_map.md`.

## What ERA Is

Every board under `keyboards/era` is an ERA board. Twenty-three boards
each ship a keyboard.json: twenty-two RP2040 boards run the SRAM-resident ERA
image and matrix engine; `sirind/brick65` (atmega32u4) runs none of that
firmware — a permanent runtime exception. All twenty-three still share the
automated-build identity under `keyboards/era/common/`:
`sirind/brick65/post_rules.mk` includes only
`system/era_build_variant_rules.mk` and `system/era_show_options.mk`.
Copy-to-RAM: `era_board_adoption.md` **Copy-To-RAM Policy**.

Three boards are split — `sirind/tomak`, `sirind/tomak79h`, `sirind/tomak79s`
— one serial wire, compact protocol. HOST-PEER is PEER-initiated; DUAL-HOST
is Left-initiated (`era_invariants.md`). Non-split boards are first-class:
copy-to-RAM, the ERA matrix engine, and every non-SYNC feature
(`era_board_adoption.md` **Non-Split Board Baseline**). QMK matrix/debounce
is the sole HID producer (`era_invariants.md`).

## The Two Cores

| Core | Owns |
| --- | --- |
| **Core0** | live USB authority, relation and route policy, the matrix engine, RGB, VIA/config, EEPROM, HID output. Builds immutable request/snapshot records and applies generation-matched results |
| **Core1** | every serial RX/TX under an owner epoch. Stage: `CORE1_FULL` only (`split/era_split_qmk_rules.mk`). Consumes immutable semantic records and bounded copied data. Does not read live QMK/VIA/EEPROM/RGB/matrix/USB/HID state and does not write EEPROM |

Placement is not ownership (`era_sram_residency_contract.md`).

## The Relation Model

The scheduler classifies the split link into a relation and picks routes from
it (`era_authority_contract.md`, `era_route_contract.md`; mode identifiers:
`era_identifier_map.md`).

| Relation | Roles | Traffic |
| --- | --- | --- |
| **LOCAL_NO_LINK** | no confirmed peer; the half runs standalone | — |
| **HOST-PEER** | USB-enumerated half = **HOST** (storage source, wire responder); the other = **PEER** (storage apply target, wire initiator) | PEER initiates confirmed traffic; HOST answers in an admitted slot |
| **DUAL-HOST** | both halves independently USB-enumerated, normally two ports on the **same** computer, one cable each. Both-hosted is always DUAL-HOST. Left initiator / Right responder | each half delivers its own HID report; no key input on the wire. Matrix, digest, and mirror routes closed |

## How State Crosses

Three boundaries:

| Boundary | What crosses | Canonical |
| --- | --- | --- |
| core0 ↔ its USB hardware | local authority facts | `era_authority_contract.md` |
| core0 ↔ core1 | published plans and results | `era_route_contract.md` |
| half ↔ half | wire sections, per relation | `era_wire_contract.md` |

The architecture's one poll is the USB SOF sample. SOF is a 1 kHz host
heartbeat with no loss interrupt. `era_usb_session_sample_frame_age()` in
`system/era_usb_session.c` holds the single SOF reading and returns an age
and an availability. The authority reducer treats unavailable as fresh; the
lighting-sleep frame-loss arm treats unavailable as not-lost. Period and
event-drive refusal: `era_authority_contract.md`. Each serviced relation's
steady state is core1's standing exchange (`era_route_contract.md`).

## Coarse Subsystem Map

Per-file ownership: `era_source_map.md`. Shape:

- relation, authority, route policy and the scheduler cold path
- wire backend (RP2040 PIO serial) and the transaction engine
- communication core: `SESSION_STATUS` and source-push initiator lanes, the
  standing exchange, storage service, responder service
- HOST-PEER replacement storage (seven EEPROM domains)
- HOST-PEER matrix source-push and HSRSP response sections
- matrix engine (scan, debounce, projection)
- diagnostics

## Glossary

| Term | Meaning |
| --- | --- |
| **HOST / PEER** | the two roles of a HOST-PEER relation (above) |
| **DUAL-HOST** | the both-hosted relation (above) |
| **source-push** | PEER publishes its local matrix snapshot to the HOST; the HOST projects both halves into one local HID report (`era_host_peer_matrix_contract.md`) |
| **HSRSP** | HOST source-response sections in the admitted response slot (`era_wire_contract.md`) |
| **matrix_ready** | no-host gate on `SESSION_STATUS` and AUTHORITY; HOST-PEER matrix admission requires it (`era_authority_contract.md` **Matrix Ready**) |
| **replacement storage** | PEER-initiated copy of the HOST EEPROM domains to the PEER: probe, transfer chunks, durable apply (`era_host_peer_storage_contract.md`) |
| **audit sweep** | seven-domain storage probe on every relation (re)establishment (`era_host_peer_storage_contract.md`) |
| **storage news value** | responder's forward-only 7-bit counter (`0` = nothing to claim); no domain identity (`era_host_peer_storage_contract.md`) |
| **durable apply** | receiving half's ERA NVM replacement of a validated staged image after ADMIT (`storage/era_nvm.c`; `era_host_peer_storage_contract.md`) |
| **responder-silence stale watch** | responder's `ERA_SPLIT_RESPONDER_SILENCE_MS` 100 watch on the core1 accepted-RX counter; silence forgets the session (`split/scheduler/era_split_transport_scheduler_internal.h`) |
| **promotion** | a former PEER that lost the relation and runs standalone on its last durable image (`split/era_split_keyboard.c`) |
| **agreed restart** | both halves commit the same act at one shared-clock deadline (`split/era_split_restart_agreement.h`; `era_wire_contract.md`; `era_closed_surface_contract.md`) |
| **reconciliation** | the pair meets at Low, then raises to the winner's stored level (`split/era_split_link.h` **Reconciliation**; `era_route_contract.md`) |
| **cold task boundary** | housekeeping cadence where storage capture/CRC/apply run, not the matrix scan (`era_route_contract.md`) |
| **CORE1_FULL** | the only selectable communication-core stage: core1 owns all wire IO (`split/era_split_qmk_rules.mk`) |
| **qwin** | no-cable standalone scan-rate window (`WIRE_QWIN`); 18 kHz per-half floor (`era_performance_gates.md`) |
| **rung** | a launcher variant that attributes one mechanism's scan-rate cost by difference against the all-on window of the same sitting; not a default-sweep profile. Live exception: `qwin_phase` turns an instrument on. Which exist: `era_build_options.md` |
