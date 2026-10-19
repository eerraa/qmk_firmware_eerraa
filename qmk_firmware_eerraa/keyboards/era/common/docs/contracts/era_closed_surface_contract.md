# ERA Closed Surface Contract

Genre: contract
Canonical for: the split-wire surfaces that are open, the surfaces that remain
closed, and the requirements any new opening must satisfy

Wire markers and body layouts are owned by `era_wire_contract.md`. Route
cadence and execution grants are owned by `era_route_contract.md`. Storage
protocol is owned by `era_host_peer_storage_contract.md`. This contract owns
the allow/reject boundary itself.

## Currently Open Exceptions

The required section eligibility is:

| Cell | Open | Closed in this id-space |
| --- | --- | --- |
| HOST-PEER push | MATRIX, AUTHORITY, STORAGE_PENDING, RESTART_ARM | INPUT, RGB, ACTIVITY, VISUAL |
| HOST-PEER response | AUTHORITY, LOCK, VISUAL, RGB, NEWS, ANCHOR | INPUT, ACTIVITY |
| DUAL-HOST push | INPUT, AUTHORITY, RGB, ACTIVITY, VISUAL, STORAGE_PENDING, RESTART_ARM | MATRIX |
| DUAL-HOST response | INPUT, ACTIVITY, AUTHORITY, VISUAL, NEWS, RGB, ANCHOR | LOCK |

LOCAL_NO_LINK is empty in both directions. A section absent from its relation
and direction cell may neither be sent nor accepted. AUTHORITY is the only
section open in all four serviced cells. STORAGE_PENDING and RESTART_ARM are
push-only; STORAGE_NEWS and TIME_ANCHOR are response-only. LOCK is HOST-PEER
response only, and MATRIX is HOST-PEER push only.

Runtime section openings add no wire operation, class id, or
responder-independent send. They ride the existing section envelopes in a slot
already opened by the relation. MATRIX instead uses the bounded input schema
below. A payload or behavior not named by an open cell, that schema, or the
explicit storage operations remains closed.

Additional bounds on open cells:

| Opening | Required boundary |
| --- | --- |
| HOST-PEER response sections | response-slot only; never an initiator-owned second send |
| replacement/DUAL-HOST storage | one storage engine; only the exact operations and directions in `era_host_peer_storage_contract.md` |
| source supersession | CHUNK_REQ may receive only the admitted ABORT_RSP/SOURCE_CHANGED alternate |
| CHUNK reserved bytes / zero-length CHUNK_RSP | no new class, op, or direction |
| DUAL-HOST runtime | per-section opening only; no family-wide runtime opening |
| DUAL-HOST ACTIVITY | derived judgment state only; no key identity, matrix, or HID-path data |
| RESTART_ARM | a bounded act that is prepared and then acted at one shared deadline |
| restart intent in AUTHORITY | must remain available independent of lighting policy |
| peer erase/reset | only EEPROM CLEAN may cause the peer erase/reset consequence |

The restart-arm cell carries an act, not a feature family. The admitted acts are
LINK_SPEED, EEPROM CLEAN, and peer-only LINK_RECOVERED. CLEAN PREPARE and
COMMIT are phases of the same EEPROM CLEAN act, and its storage quarantine is
derived local admission state rather than a new wire fact.

An act whose consequence cannot be expressed as prepare-then-act at one shared
deadline remains closed. Reopening that boundary requires a new bounded act
entry with explicit parameter validation, confirmation/deadline behavior, and
a defined reset consequence.

Responder-independent send remains closed in every serviced relation. The
responder is a role — HOST-PEER HOST or DUAL-HOST Right — not a relation name.
Opening an independent responder request would create a second initiator and
requires a deliberate protocol opening, not reuse of an existing response slot.

Bounded debounced snapshot history is open only from HP PEER to HOST under
`era_host_peer_matrix_contract.md`. DELTA/FULL/RESET and FENCE/BIND/FLOW require
current owner/relation, authority and storage admission independently of parser
recognition. Legacy MATRIX section injection remains rejected. No DH input
lifetime or action state may be created by these bodies.

## Closed Until Explicitly Opened

The following remain closed even though adjacent markers or classes exist:

- HOST-PEER INPUT and ACTIVITY in both directions, RGB and visual baseline in
  source-push. That relation is one input pipeline: the PEER does not resolve
  keycodes, the HOST tapping engine already sees both halves' key events, and
  HOST RGB/visual state already has the response carrier.
- DUAL-HOST LOCK. Each half already receives its own host LED report.
- DUAL-HOST MATRIX, matrix digest, and mirror execution.
- per-frame RGB event transport; runtime RGB is latest-state.
- storage shape, direction, domain, or authority beyond
  `era_host_peer_storage_contract.md`.
- V16 ATTEST/BEGIN/DATA/COMMIT/ABORT storage semantics as a compatibility
  surface.
- any writer of the storage summary response's relation time-anchor seat; that
  seat must remain zero while TIME_ANCHOR has its one relation carrier.
- a DUAL_RUNTIME_BUNDLE or class 0x6x runtime family; the reserved family is
  not a future-runtime reservation.
- class 0x40 ERROR_NACK as an active encoder/decoder surface.
- digest-only matrix response or a full row-array matrix payload.
- bulk-page payloads other than the storage contract's CHUNK_RSP response and
  PUSH_CHUNK_REQ request.
- direct HID injection, remote HID endpoints, DUAL-HOST matrix replay, and
  unbounded full key-event replay.

A newly available symbol, marker, reserved class, or body byte does not by
itself open any of these surfaces.

## Storage Lane Boundary

Only the confirmed relation's initiator selects storage: HOST-PEER PEER or
DUAL-HOST Left. The responder answers only inside the matching admitted request
slot.

Transfer receipt is not durable completion. Durable apply may be published only
after generation-matched Core0 write/readback, runtime reload, Core1 restart,
and relation revalidation. Core1 never reads live
QMK/VIA/EEPROM/RGB/matrix/USB/HID state and never writes EEPROM.

Normative ownership: `era_host_peer_storage_contract.md`,
`era_invariants.md` **Core Lifetime And Thread Boundary**, and
`storage/era_nvm.c`.

## Classifier Rule

Recognizing a reserved class so it can be rejected safely is not route,
admission, response, or execution authority. Classification may identify an
unsupported value and still require the caller to reject it.

## Source Enforcement Anchors

- required section cells and compile-time closed-cell assertions:
  `split/era_split_wire_protocol.h`;
- linked runtime eligibility table and payload classifier:
  `split/era_split_wire_payload.c`;
- responder section-mask admission:
  `split/communication_core/era_split_communication_core_responder_service.c`;
- storage bulk-lane validation, including the permanently zero relation
  time-anchor seat:
  `split/communication_core/era_split_communication_core_storage.c`;
- ELF/runtime evidence for the same cells:
  `manuals/era_performance_gates.md`.

These anchors implement the boundary; they do not replace the requirement
table above.

## Opening Rule

Opening any closed item requires the same logical change to provide:

- the contract change here and in every affected authority/wire/route/storage
  contract;
- source implementation and input/admission rejection for malformed or
  out-of-scope forms;
- source/ELF evidence that unrelated cells and classes remain closed;
- the applicable build and performance gates;
- compact real-device evidence when the hardware-visible behavior changes.

Naming a future slice, adding a marker, or recognizing a reserved id does not
open runtime behavior.

Two changes can look like openings without being one:

- **carrier change** — moving an already admitted section between existing
  scheduling carriers changes route ownership but not its wire body or
  eligibility cell;
- **narrowing** — refusing a previously admitted form shrinks the accepted
  surface. It still owes wire-visible regression checks, but not an opening
  justification.

The eligibility cells are the definitive open/closed boundary. If they are
unchanged, a route refactor has not silently opened a new section surface.
