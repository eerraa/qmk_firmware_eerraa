# ERA Overview

Status: active
Genre: contract
Canonical for: the ERA mental model and shared vocabulary. The plain-language
architecture picture and the glossary live only here; every rule, ownership
row, and symbol name stays canonical in the documents this file points to.
Read when: first, before the invariants and the task-specific set, so the
jargon-heavy documents below decode on first contact.

## What ERA Is

Every board under `keyboards/era` is an ERA board: twenty-two RP2040 boards
take the whole ERA layer, and `sirind/brick65` (atmega32u4) takes none of it — a
permanent exception, not a debt. All share one common layer under
`keyboards/era/common/`, and every RP2040 one runs the whole image from SRAM
(copy-to-RAM) so neither core fetches flash at runtime
(`manuals/era_board_adoption.md`'s **Copy-To-RAM Policy**).

Three are split — `sirind/tomak`, `tomak79h`, `tomak79s` — and the custom
split-communication architecture layered on QMK is the largest body of work
here: the two halves talk over a single serial wire with a compact,
PEER-initiated protocol. Non-split boards are first-class rather than a reduced
build — copy-to-RAM, the ERA matrix engine, and every non-SYNC feature
(`manuals/era_board_adoption.md`'s **Non-Split Board Baseline**). On every board,
QMK's own matrix/debounce path stays the sole HID producer, which is canonical
in `contracts/era_invariants.md`.

## The Two Cores

- **Core0** owns everything live: USB authority, relation and route policy,
  the matrix engine, RGB, VIA/config, EEPROM, and HID output. It builds
  immutable request/snapshot records and applies generation-matched results.
- **Core1** is the only active wire backend executor — every serial RX/TX
  runs on core1 under an owner epoch. It consumes only immutable semantic
  records and bounded copied data; it never reads live QMK state or writes
  EEPROM. `CORE1_FULL` is the default and only stage.

Placement never moves ownership: see `contracts/era_sram_residency_contract.md`.

## The Relation Model

The scheduler classifies the split link into a relation and picks routes from it
(authority and mode rules: `contracts/era_authority_contract.md`,
`contracts/era_route_contract.md`; mode identifiers:
`maps/era_identifier_map.md`).

- **LOCAL_NO_LINK** — no confirmed peer; a half runs standalone.
- **HOST-PEER** — one half is USB-enumerated to a computer (the **HOST**,
  storage source and wire responder); the other has no USB host (the **PEER**,
  storage apply target and wire initiator). The PEER initiates all confirmed
  traffic; the HOST answers only in an admitted slot.
- **DUAL-HOST** — both halves independently USB-enumerated, normally into two
  ports on the **same** computer, one cable per half. Each half delivers its own
  HID report, so no key input crosses the wire first: this is the low-latency
  configuration and the preferred gaming mode, and it exists to remove the extra
  hop HOST-PEER imposes on the PEER half. Nothing requires two machines —
  reading it that way makes the relation look exotic instead of routine, and it
  is why matrix, digest, and mirror routes stay closed here: with both halves
  reporting directly there is no matrix to forward. Wire ownership is Left
  initiator / Right responder, and both-hosted is always DUAL-HOST.

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
10 ms freshness window it serves rather than chosen. A design that event-drives
this believes a dead host behind a live cable is still a host.

It is one sampler, not two polls. `era_usb_session_sample_frame_age()`
(`common/system/era_usb_session.c`) holds the single SOF reading, and both the
authority reducer and the lighting-sleep frame-loss arm call it at their own
call site rather than reading an age an earlier pass left behind. It returns an
age **and** an availability because the two need opposite answers when there is
no reading yet — fresh to the reducer, not-lost to the sleep decision — so one
boolean would silently invert one of them.

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

- **HOST / PEER** — the two roles of a HOST-PEER relation (above).
- **DUAL-HOST** — the both-hosted relation (above).
- **source-push** — the PEER pushing its local matrix (key) state to the HOST
  so the HOST projects both halves into one local HID report.
- **HSRSP** — HOST source-response sections: the HOST-owned lock/visual/
  RGB-state/relation-time-anchor data returned in the admitted response slot.
- **matrix_ready** — a `SESSION_STATUS` gate that must be set before HOST-PEER
  matrix payloads are admitted.
- **replacement storage** — the PEER-initiated protocol that copies the HOST's
  EEPROM config domains to the PEER (probe → transfer chunks → durable apply).
- **audit sweep** — the mandatory seven-domain storage probe run once on every
  relation (re)establishment.
- **storage news value** — the responder's forward-only 7-bit counter (`0` =
  nothing to claim), stepped once per settled config capture that departs from
  the last agreement; its only job is to
  tell the initiator to run a whole-family `SYNC_STATUS` summary. It carries no
  domain identity (`contracts/era_host_peer_storage_contract.md`).
- **durable apply** — the PEER's sliced EEPROM write of a CRC-validated staged
  image, with the wire kept alive by core1's liveness beat.
- **flash guard / `fwg`** — the EEPROM commit-window recorder; on the SRAM image
  it records the window and does not stop core1.
- **responder-silence stale watch** — the responder's 100 ms watch on the core1
  accepted-RX counter; no accepted frame for the limit forgets the session. It
  is fed by the relation's own traffic, so the limit is a constant of its own
  and not a multiple of any status period.
- **promotion** — a former PEER that lost the relation (e.g. cable pull) and
  runs standalone on its last durable image.
- **agreed restart** — both halves of a pair committing the same **act** at one
  instant: the commanded half requests over the AUTHORITY section, the
  relation's initiator arms with a shared-clock deadline over the
  `RESTART_ARM` push section, and each half runs the act's own preparation at
  that deadline. An act that resets (EEPROM CLEAN) resets immediately after
  prepare; the link-speed act does not — its prepare is the runtime divider
  change. The mechanism knows neither user
  (`split/era_split_restart_agreement.h`; wire in
  `contracts/era_wire_contract.md`, what the cell is open for in
  `contracts/era_closed_surface_contract.md`).
- **reconciliation** — the link lane's rule that the pair *meets at Low, then
  raises to the winner's stored level*: every half boots the wire at Low;
  once `SESSION_STATUS` has opened the relation and the standing surface
  (including the time-anchor) has crossed, the winner (DUAL-HOST Left,
  HOST-PEER HOST) requests the agreed link act with its stored level as
  param and both halves `set_speed` at the shared deadline; a raise that
  fails reverts *running* to Low for the rest of the session and writes
  nothing. The listener's High → Medium → Low ring remains the recovery for
  a pair already at two running rates, not the common boot
  (`split/era_split_link.h`; why it is a route-layer fact in
  `contracts/era_route_contract.md`).
- **cold task boundary** — the housekeeping cadence (not matrix scan) where
  storage capture/CRC/apply run.
- **CORE1_FULL** — the default communication-core stage: core1 owns all wire IO.
- **qwin** — the no-cable standalone scan-rate benchmark window (`WIRE_QWIN`);
  its 18 kHz per-half floor is canonical in `manuals/era_performance_gates.md`.
- **rung** — a build profile that exists only to attribute a measurement: the
  all-on image with exactly **one** mechanism turned off, so the difference
  between its window and the all-on window of the same sitting is that
  mechanism's contribution. A batch that lands several mechanisms together is
  taken apart with a ladder of them. Three properties make it a rung rather than
  a profile: it goes through the launcher, so its artifact carries a manifest
  and a stem of its own; it sits **outside the default sweep**, so no gate sweep
  builds it; and **its own figure is never a comparison point** — only the
  difference is. A rung retires with the batch it bisected, and four have
  (`qwin_piooff`, `qwin_wfeoff`, `qwin_gateoff`, `qwin_limit20`). `qwin_phase`
  is the standing exception: it turns an instrument *on* rather than a mechanism
  off, and is a rung for the other two reasons, because the instrument costs
  scan rate. Which rungs exist is `manuals/era_build_options.md`'s.
