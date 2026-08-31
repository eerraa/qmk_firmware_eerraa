# ERA Closed Surface Contract

Genre: contract
Canonical for: what is open on the split wire, what stays closed, and the
Opening Rule any opening must pay

## Currently Open Exceptions

The linked table `g_era_split_wire_section_eligibility` in
`split/era_split_wire_payload.c` is the only opener, sized by
`ERA_SPLIT_WIRE_SECTION_ELIGIBILITY_MODES` 5.
`era_split_wire_eligible_sections()` in that file returns the relation-keyed
entry. `era_split_transport_scheduler_build_standing_plan()` in
`split/era_split_transport_scheduler.c` copies both direction bytes onto the
standing plan; `era_split_communication_core_standing_service_once()` in
`split/communication_core/era_split_communication_core_standing.c` claims a
section only when that plan bit is set.
`era_split_communication_core_responder_service_once()` in
`split/communication_core/era_split_communication_core_responder_service.c`
refuses a push whose section mask has any bit outside the entry. The ELF gate
reads those bytes (`era_performance_gates.md`). Markers, bodies, and the
four-macro open list: `era_wire_contract.md` **Section eligibility**.

Both roles of one relation share one pair. `LOCAL_NO_LINK` is empty in both
directions. A section absent from the entry is neither sent nor accepted.
This document names what is open; the table enforces it. Recomputed from
`ERA_SPLIT_WIRE_SECTION_ELIGIBLE_*` in `split/era_split_wire_protocol.h`:

| Cell | Macro | Open | Closed in this id-space |
| --- | --- | --- | --- |
| HOST-PEER push | `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_PUSH` | MATRIX, AUTHORITY, STORAGE_PENDING, RESTART_ARM | INPUT, RGB, ACTIVITY, VISUAL |
| HOST-PEER rsp | `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_HOST_PEER_RSP` | AUTHORITY, LOCK, VISUAL, RGB, NEWS, ANCHOR | INPUT, ACTIVITY |
| DUAL-HOST push | `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH` | INPUT, AUTHORITY, RGB, ACTIVITY, VISUAL, STORAGE_PENDING, RESTART_ARM | MATRIX |
| DUAL-HOST rsp | `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP` | INPUT, ACTIVITY, AUTHORITY, VISUAL, NEWS, RGB, ANCHOR | LOCK |

AUTHORITY is the one section in all four serviced cells. STORAGE_PENDING and
RESTART_ARM are push-only; STORAGE_NEWS and TIME_ANCHOR are rsp-only. LOCK is
HOST-PEER rsp only. MATRIX is HOST-PEER push only. Closing AUTHORITY would
re-open the `SESSION_STATUS` post-relation cadence (`era_route_contract.md`).
STORAGE_PENDING composition: `era_host_peer_storage_contract.md`'s
**Diagnostics**.

Every runtime opening adds **no wire operation, no class id, and no responder
independent send**, and rides the existing `0x20`/`0x21` envelope pair in slots
the relation already opens. The list is exhaustive — a payload or behavior not
named here is closed.

| Opening | May never carry |
| --- | --- |
| HOST-PEER MATRIX and AUTHORITY | both in one compact frame (`era_wire_contract.md`) |
| `HOST_PEER_HOST_SOURCE_RSP` | any initiator-owned send; any section the HOST-PEER rsp row does not list. Response-slot only |
| Replacement storage | anything beside the exact operation set in `era_host_peer_storage_contract.md`. Initiator-sent; responder only in admitted slots |
| DUAL-HOST storage | a second engine, or a `SESSION_STATUS` flag bit. Same contract, Left initiator / Right responder |
| Source supersession | any `CHUNK_REQ` answer other than the admitted `ABORT_RSP`/`SOURCE_CHANGED` alternate (`era_wire_contract.md`) |
| `CHUNK_REQ` reserved bytes / zero-length `CHUNK_RSP` | a new class, op, or direction. Body: `era_wire_contract.md` |
| DUAL-HOST runtime | a family-wide opening. Per section, not per family. Right answers only in slots the Left opens |
| DUAL-HOST ACTIVITY | key identity, matrix, anything that enters the HID path |
| RESTART_ARM | an act whose consequence is not prepare-then-act at one shared deadline |
| Restart intent (AUTHORITY flags) | a lighting-gated carrier (`era_wire_contract.md`) |
| Wire-triggered peer erase and reset | any act outside the two the arm names. Validator refuses any other; work is a local dispatch |

The DUAL-HOST ACTIVITY body carries a derived judgment-window flag and
key-input counters plus a shared-clock instant — no key identity, no matrix,
nothing that enters the HID path.

The restart-arm cell **carries an act, not a feature**, and that is what this
cell is open for. The link switch was its first user and the EEPROM clean is
its second; a third is a new act value in an existing body, not a new opening.
CLEAN's PREPARE and COMMIT are phases of that same second act. The storage
quarantine they drive is derived local admission state and crosses as no new
wire fact (`era_wire_contract.md`).

> **REFUSED:** an act whose consequence is not prepare-then-act at one shared deadline, resetting only when the act table says so.
> **WHY:** a wire that can make a peer do arbitrary work is a different surface from a bounded agreement on an existing body.
> **REOPENS:** a new act table entry that is still prepare-then-act at one shared deadline.

These openings do not authorize responder independent send. The prohibition is
on the *role*, not on one relation's name for it: the responder is the
HOST-PEER HOST or the DUAL-HOST Right.

> **REFUSED:** responder independent send, including responder-owned storage data.
> **WHY:** the responder is a role — HOST-PEER HOST or DUAL-HOST Right — and an independent send is a second initiator on that wire.
> **REOPENS:** a relation whose responder owns a request of its own.

## Closed Until Explicitly Opened

| Closed | Enforcement |
| --- | --- |
| responder independent send, including responder-owned storage data | role prohibition above |
| any `HOST_PEER_HOST_SOURCE_RSP` section the HOST-PEER rsp row does not list | wire eligibility |
| HOST-PEER RGB in source-push, INPUT in both directions, ACTIVITY in both directions, visual baseline in source-push | each compile-asserted beside its eligibility entry in `split/era_split_wire_protocol.h`. That relation is one pipeline: the PEER never resolves keycodes, so the HOST's composed rows already carry a PEER-held layer key and the PEER's hits reach the HOST as projected matrix rows; the PEER renders the HOST's RGB config, so the response direction is the relation's only RGB carrier; the HOST's tapping engine sees every key of both halves as a local event, so a counter image would be a second carrier for facts the engine holds first-hand. DUAL-HOST cells of those sections are the open rows above. AUTHORITY is not covered: it carries no input and no render state. Per-cell REFUSED: `era_wire_contract.md` **Closed ids** |
| DUAL-HOST LOCK | `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP` excludes it; no companion `_Static_assert`. Each half already receives its own host LED report (`era_wire_contract.md`) |
| DUAL-HOST MATRIX, digest, and mirror execution | MATRIX: `_Static_assert` on `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH`. Digest and mirror: `era_invariants.md` |
| per-frame RGB event transport | closed; runtime sections are latest-state |
| `EEPROM_SYNC` shape, direction, domain, or authority outside the exact contract in `era_host_peer_storage_contract.md` | class is in force; the tripwire is the surface |
| removed V16 `ATTEST`/`BEGIN`/`DATA`/`COMMIT`/`ABORT` storage semantics | not a compatibility surface |
| any writer of the storage summary response's relation time-anchor seat | reads zero **permanently**; `era_split_communication_core_storage_validate_wire_payload()` in `split/communication_core/era_split_communication_core_storage.c` is that enforcement. Opening the DUAL-HOST `TIME_ANCHOR` cell did not open this seat |
| a `DUAL_RUNTIME_BUNDLE` symbol or class `0x6x` id, **permanently** | no compiled id and no reservation; `era_split_wire_classify_payload()` in `split/era_split_wire_payload.c` rejects it through its `default` arm. Closed ids: `era_wire_contract.md` |
| class `0x40` `ERROR_NACK` | no encoder writes it; nothing switches on the kind; `default` reject. Retired kind numbering: `era_identifier_map.md` |
| digest-only matrix response | closed |
| full row-array matrix payload | `transport_master()` / `transport_slave()` are absent from `split/era_split_transport.c` |
| bulk-page payloads other than the storage contract's `CHUNK_RSP` response and its `PUSH_CHUNK_REQ` request | `era_wire_contract.md` |
| direct HID injection, remote HID endpoints, remote matrix replay, and full key-event replay | `era_invariants.md` |

> **REFUSED:** any writer of the storage summary response's relation time-anchor seat.
> **WHY:** `TIME_ANCHOR` is the anchor's one carrier in every relation; the seat is reserved for nothing.
> **REOPENS:** never, while `era_split_communication_core_storage_validate_wire_payload()` in `split/communication_core/era_split_communication_core_storage.c` requires those bytes zero.

## Storage Lane Boundary

- Only the confirmed relation's initiator selects storage — the HOST-PEER PEER,
  or the DUAL-HOST Left.
- The responder responds only inside the admitted matching request slot.
- Transfer receipt is not durable completion. Only generation-matched core0
  write/readback, runtime reload, Core1 restart, and relation revalidation may
  publish durable apply (`era_nvm_replace()` in `storage/era_nvm.c`).
- Core1 never reads live QMK/VIA/EEPROM/RGB/matrix/USB/HID state and never
  writes EEPROM (`era_overview.md` **The Two Cores**).

## Classifier Rule

Recognizing a reserved class so it can be rejected safely is not route,
admission, response, or execution authority.
`era_split_wire_classify_payload()` in `split/era_split_wire_payload.c` is that
reject.

## Opening Rule

Opening any closed item requires, in the same logical change:

- update of this contract and every affected authority/wire/route/domain contract;
- source implementation;
- source and ELF proof that unrelated surfaces remain closed;
- applicable build and performance gates;
- compact real-device evidence when hardware behavior changes.

Naming a later slice never opens its runtime surface.

Two kinds of change read like an opening and are not. Neither pays this rule:

- **a change of carrier.** Which core plans a section, and which route or answer carries it, is a scheduling change inside surfaces already open. The surface is the frame, the marker, the body and the eligibility cell — not the lane they travel on.
- **a narrowing.** Refusing what was previously admitted — a section an answer may no longer carry, a class the classifier now drops to its `default` reject — shrinks the accepted surface. It is still wire-visible and still owes its gates, but this rule fires on opening.

The eligibility table is the proof in both directions: if its bytes read the
same before and after, no surface moved.
