# ERA Overview

Genre: contract
Canonical for: the ERA mental model and shared vocabulary. The plain-language
architecture picture and the glossary live only here; every rule, ownership
row, and symbol name stays canonical in the documents this file points to.

## What ERA Is

Every board under `keyboards/era` is an ERA board. Twenty-two RP2040 boards
take the whole ERA firmware layer; `sirind/brick65` (atmega32u4) takes none
of that firmware — a permanent runtime exception, not a debt. All twenty-three
still share one automated-build identity contract under `keyboards/era/common/`:
`sirind/brick65/post_rules.mk` includes only the common make-time build-variant
validator and option printer. Every RP2040 board runs the whole image from SRAM
so neither core fetches flash at runtime
(`manuals/era_board_adoption.md`'s **Copy-To-RAM Policy**).

Three boards are split — `sirind/tomak`, `tomak79h`, `tomak79s` — and talk
over a single serial wire with a compact, PEER-initiated protocol. Non-split
boards are first-class, not a reduced build: copy-to-RAM, the ERA matrix
engine, and every non-SYNC feature
(`manuals/era_board_adoption.md`'s **Non-Split Board Baseline**). On every
board, QMK's own matrix/debounce path stays the sole HID producer
(`contracts/era_invariants.md`).

## The Two Cores

| Core | Owns | Must not |
| --- | --- | --- |
| **Core0** | everything live: USB authority, relation and route policy, the matrix engine, RGB, VIA/config, EEPROM, HID output. Builds immutable request/snapshot records and applies generation-matched results | — |
| **Core1** | every serial RX/TX, under an owner epoch. Default and only stage: `CORE1_FULL`. Consumes only immutable semantic records and bounded copied data | read live QMK state; write EEPROM |

Placement never moves ownership (`contracts/era_sram_residency_contract.md`).

## The Relation Model

The scheduler classifies the split link into a relation and picks routes from
it (`contracts/era_authority_contract.md`, `contracts/era_route_contract.md`;
mode identifiers: `maps/era_identifier_map.md`).

| Relation | Roles | Traffic |
| --- | --- | --- |
| **LOCAL_NO_LINK** | no confirmed peer; the half runs standalone | — |
| **HOST-PEER** | the USB-enumerated half is the **HOST** (storage source, wire responder); the other is the **PEER** (storage apply target, wire initiator) | PEER initiates all confirmed traffic; HOST answers only in an admitted slot |
| **DUAL-HOST** | both halves independently USB-enumerated, normally into two ports on the **same** computer, one cable per half. Both-hosted is always DUAL-HOST. Wire ownership is Left initiator / Right responder | each half delivers its own HID report; no key input crosses the wire. Matrix, digest, and mirror routes stay closed — there is no matrix to forward |

## How State Crosses

There are exactly three boundaries, and each carries one mechanism:

| Boundary | What crosses | Where it is canonical |
| --- | --- | --- |
| core0 ↔ its own USB hardware | local authority facts | `contracts/era_authority_contract.md` |
| core0 ↔ core1 | published plans and results | `contracts/era_route_contract.md` |
| half ↔ half | wire sections, per relation | `contracts/era_wire_contract.md` |

The rule for all three is the same: **the side that changes publishes, and the
side that consumes is woken by an edge.** Cost then scales with how often the
fact changes and not with how often anyone looks.

**Exactly one thing polls, and it is named rather than tolerated.** The USB SOF
counter is a 1 kHz host heartbeat with no loss interrupt, so its absence is
observable only by noticing the count stopped. The authority sample is therefore
a deadline and not a cadence over an event, and its period is derived from the
10 ms freshness window it serves rather than chosen. The refusal of event-driving
this sample is `contracts/era_authority_contract.md`'s.

It is one sampler, not two polls. `era_usb_session_sample_frame_age()`
(`common/system/era_usb_session.c`) holds the single SOF reading and returns an
age **and** an availability, because the authority reducer and the lighting-sleep
frame-loss arm need opposite empty-state answers — fresh to the reducer,
not-lost to the sleep decision. Both callers invoke it at their own call site
rather than reading an age an earlier pass left behind.

Nothing else polls. Core0 wakes on a published result, and each relation's lane
carries the peer's session facts in its own AUTHORITY section
(`contracts/era_wire_contract.md`); neither relation originates a periodic core0
frame, and both run core1's standing exchange as their steady state. What
differs between them is the section set — HOST-PEER carries a matrix, so its
PEER keeps one event-driven core0 route (`contracts/era_route_contract.md`).

Any new state that crosses the half-to-half boundary rides the relation's lane
with an eligibility entry and may not introduce a carrier of its own; the
enforceable form is in `contracts/era_closed_surface_contract.md`.

## Coarse Subsystem Map

Per-file ownership is canonical in `maps/era_source_map.md`; this is the shape:

- relation/authority/route policy and the scheduler cold path;
- the wire backend (RP2040 PIO serial) and the transaction engine above it;
- the communication core (core1 lanes: session, heartbeat, source-push,
  storage, responder service);
- HOST-PEER replacement storage (seven portable EEPROM domains);
- HOST-PEER matrix source-push and the HSRSP response sections;
- the matrix engine (scan, debounce, projection);
- diagnostics.

## Glossary

| Term | Meaning |
| --- | --- |
| **HOST / PEER** | the two roles of a HOST-PEER relation (above) |
| **DUAL-HOST** | the both-hosted relation (above) |
| **source-push** | the PEER pushing its local matrix (key) state to the HOST so the HOST projects both halves into one local HID report |
| **HSRSP** | HOST source-response sections: the HOST-owned lock/visual/RGB-state/relation-time-anchor data returned in the admitted response slot |
| **matrix_ready** | a `SESSION_STATUS` gate that must be set before HOST-PEER matrix payloads are admitted |
| **replacement storage** | the PEER-initiated protocol that copies the HOST's EEPROM config domains to the PEER (probe → transfer chunks → durable apply) |
| **audit sweep** | the mandatory seven-domain storage probe run once on every relation (re)establishment |
| **storage news value** | the responder's forward-only 7-bit counter (`0` = nothing to claim), stepped once per settled config capture that departs from the last agreement; its only job is to tell the initiator to run a whole-family `SYNC_STATUS` summary. It carries no domain identity (`contracts/era_host_peer_storage_contract.md`) |
| **durable apply** | the receiving half's synchronous ERA NVM replacement of a fully validated staged image after ADMIT; public EEPROM remains old until NVM commit, while core1 keeps the relation alive from SRAM |
| **responder-silence stale watch** | the responder's 100 ms watch on the core1 accepted-RX counter; no accepted frame for the limit forgets the session. Fed by the relation's own traffic, so the limit is a constant of its own and not a multiple of any status period |
| **promotion** | a former PEER that lost the relation (e.g. cable pull) and runs standalone on its last durable image |
| **agreed restart** | both halves of a pair committing the same **act** at one shared-clock deadline: the commanded half requests over the AUTHORITY section, the relation's initiator drives the act's preparation and commit phases over the existing `RESTART_ARM` push section, and AUTHORITY carries the responder's matching state. EEPROM CLEAN is the checked-preparation user: both halves invalidate and verify their own boot magic under storage quarantine, and no commit deadline exists until both report PREPARED. It then resets at the deadline with no further EEPROM write. The link-speed act still applies its runtime divider at the deadline and does not reset. The mechanism knows neither user (`split/era_split_restart_agreement.h`; wire in `contracts/era_wire_contract.md`, what the cell is open for in `contracts/era_closed_surface_contract.md`) |
| **reconciliation** | the link lane's rule that the pair *meets at Low, then raises to the winner's stored level*: every half boots the wire at Low; once `SESSION_STATUS` has opened the relation and the standing surface (including the time-anchor) has crossed, the winner (DUAL-HOST Left, HOST-PEER HOST) requests the agreed link act with its stored level as param and both halves `set_speed` at the shared deadline; a raise that fails reverts *running* to Low for the rest of the session and writes nothing. The listener's High → Medium → Low ring remains the recovery for a pair already at two running rates, not the common boot (`split/era_split_link.h`; why it is a route-layer fact in `contracts/era_route_contract.md`) |
| **cold task boundary** | the housekeeping cadence (not matrix scan) where storage capture/CRC/apply run |
| **CORE1_FULL** | the default communication-core stage: core1 owns all wire IO |
| **qwin** | the no-cable standalone scan-rate benchmark window (`WIRE_QWIN`); its 18 kHz per-half floor is canonical in `manuals/era_performance_gates.md` |
| **rung** | a build variant that exists only to attribute a measurement: the all-on image with exactly **one** mechanism turned off, so the difference between its window and the all-on window of the same sitting is that mechanism's contribution. A batch that lands several mechanisms together is taken apart with a ladder of them. Three properties make it a rung rather than a profile: it goes through the launcher, so its artifact carries a manifest and a stem of its own; it sits **outside the default sweep**, so no gate sweep builds it; and **its own figure is never a comparison point** — only the difference is. A rung retires with the batch it bisected, and four have (`qwin_piooff`, `qwin_wfeoff`, `qwin_gateoff`, `qwin_limit20`). `qwin_phase` is the standing exception: it turns an instrument *on* rather than a mechanism off, and is a rung for the other two reasons, because the instrument costs scan rate. Which rungs exist is `manuals/era_build_options.md`'s |
