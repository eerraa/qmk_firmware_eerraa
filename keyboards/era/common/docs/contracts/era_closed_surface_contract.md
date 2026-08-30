# ERA Closed Surface Contract

Genre: contract
Canonical for: what is open on the split wire, what stays closed, and the
Opening Rule any opening must pay

## Currently Open Exceptions

Which sections a relation may carry is the linked `const`
`ERA_SPLIT_WIRE_SECTION_ELIGIBLE_*` table in `era_split_wire_protocol.h`
(planner and admission AND against it). This document names what is open; the table enforces it; `era_performance_gates.md` reads its bytes; markers and cell eligibility are `era_wire_contract.md`'s. Every entry adds **no wire operation, no class id, and no responder independent send**, and rides the existing `0x20`/`0x21` envelope pair in slots the relation already opens. The list is exhaustive — a payload or behavior not named here is closed.

| Exception | Relation / cell | Marker / body | May never carry | Owner |
| --- | --- | --- | --- | --- |
| HOST-PEER source-push packed matrix and AUTHORITY | HOST-PEER push, separate routes | both sections; they do not fit one compact payload | both in one frame (layout walk refuses) | `era_wire_contract.md` |
| AUTHORITY | every serviced relation, both directions | the one section eligible everywhere; precondition for `SESSION_STATUS` stopping post-relation, not an addition on top of it | closure (would re-open the `SESSION_STATUS` cadence) | `era_route_contract.md` |
| `HOST_PEER_HOST_SOURCE_RSP` | response-slot only | only sections `era_wire_contract.md` lists as open | any other section or an initiator-owned send | `era_wire_contract.md` |
| Replacement storage | initiator-sent push; responder only in admitted slots | exact operation set | anything beside that set; no responder independent send | `era_host_peer_storage_contract.md` |
| DUAL-HOST storage | `DUAL_HOST_LEFT` initiator, `DUAL_HOST_RIGHT` responder | same engine, operations, arbitration, wire roles | a `SESSION_STATUS` flag bit | `era_host_peer_storage_contract.md` |
| Source supersession | no payload, no direction | only the admitted `ABORT_RSP`/`SOURCE_CHANGED` alternate for `CHUNK_REQ` | any other `CHUNK_REQ` answer | `era_host_peer_storage_contract.md` |
| `CHUNK_REQ` reserved bytes | same admitted response slot | 24-bit chunk-CRC hint; zero-length `CHUNK_RSP` content-match | new class, op, or direction | `era_wire_contract.md` |
| DUAL-HOST runtime | per section, not per family; INPUT both directions | Right answers only in slots the Left opens; cadence changes only how often, not which core decides when | a family-wide opening | `era_route_contract.md` |
| Storage-exclusive DUAL-HOST answer | same INPUT slot | section-less ACK while the response plan is empty | a different surface than an idle responder with no section due | `era_route_contract.md` |
| Relation time-anchor | both relations; already-admitted HSRSP response slots | one existing marker in one existing DUAL-HOST response cell | new body or new deferral; yields to every other section | `era_wire_contract.md`; `era_route_contract.md` |
| DUAL-HOST RGB-state | both cells (push exists because response alone carries only the responder's changes) | response-direction RGB-state body unchanged; sleep bit zero at capture and skipped at apply | a new body | `era_authority_contract.md` |
| DUAL-HOST ACTIVITY | both cells; no service reason | reused retired-section marker; judgment-window flag, key-input counters, shared-clock instant | key identity, matrix, anything that enters the HID path | `era_wire_contract.md` |
| DUAL-HOST visual pressed-baseline | both cells; RGB policy gate | one body of one width; HOST-PEER response reason-plus-baseline form unchanged | a new body | `era_authority_contract.md` |
| Storage news | both relations; already-admitted HSRSP response slots; probe scheduling reuses the exact existing storage operations | one existing marker, one-byte body; bits 0..6 news, bit7 pending; zero admitted; no reserved-bit reject | a new section or a reserved-bit reject on this byte | `era_wire_contract.md` |
| Storage-pending | push cell of both relations | marker `0x40` (never assigned in the push id space), one-byte body; rides `RUNTIME_SECTION_PUSH` | new op, class, cadence, or responder independent send | `era_wire_contract.md`; `era_host_peer_storage_contract.md`'s **Diagnostics** |
| Restart arm | push cell of both relations | marker `0x80` (same unread-id reading `0x40` took), five-byte body; rides `RUNTIME_SECTION_PUSH` | an act whose consequence is not prepare-then-act at one shared deadline | `era_wire_contract.md` |
| Restart intent | AUTHORITY flags, both relations, both directions | bits 3..7 of that byte (`0xFF` full); no new body, marker, mask, or eligibility cell | a lighting-gated carrier | `era_wire_contract.md` |
| Wire-triggered peer erase and reset | the two agreed acts only | arm names the act; validator refuses any other; work is a local dispatch | any other reset-over-wire, or a frame that describes the work | `era_wire_contract.md` |

The DUAL-HOST ACTIVITY body carries a derived judgment-window flag and key-input counters plus a shared-clock instant — no key identity, no matrix, nothing that enters the HID path. Marker reuse and its accepted capture ambiguity are `era_wire_contract.md`'s. The window derivation from the tapping bridge's off-by-default options is what keeps a fresh-defaults image from ever making the section due.

The restart-arm cell **carries an act, not a feature**, and that is what this cell is open for. The link switch was its first user and the EEPROM clean is its second; a third is a new act value in an existing body, not a new opening. The cell may never carry an act whose consequence is anything but "prepare, then act at one shared deadline, resetting only when the act table says so" — a wire that can make a peer do arbitrary work is a different surface. CLEAN's `PREPARE(param=1, T=0)` and `COMMIT(param=2, T!=0)` are phases of that same second act: they retain the five-byte body, marker, push direction, standing cadence and eligibility cells. The storage quarantine they drive is derived local admission state and crosses as no new wire fact (`era_wire_contract.md`).

The response section mask has all eight markers assigned, so the responder's half of the pending fact and of the restart agreement has no marker to take: pending rides `STORAGE_NEWS` bit7 (bits 0..6 the news value; the byte admits its full width), and restart intent rides AUTHORITY bits 3..7. CLEAN reuses the existing act/param/bit7 tuple as REQUEST, PREPARED and COMMIT_ARMED; it adds no bit; CLEAN's user/API parameter remains zero. Relation identity scopes every phase. This spends the AUTHORITY flags byte's last reserved bit; the byte joins `STORAGE_NEWS` in having none left. Alternatives that would buy another are `era_wire_contract.md`'s.

Device falsifiers are owed as compact evidence for `STORAGE_PENDING`, the paired-rise of `STORAGE_NEWS` bit7, and the restart acts.

These exceptions do not authorize responder independent send. The prohibition is on the *role*, not on one relation's name for it: the responder is the HOST-PEER HOST or the DUAL-HOST Right.

## Closed Until Explicitly Opened

| Closed | Enforcement / why |
| --- | --- |
| responder independent send, including responder-owned storage data | role prohibition above |
| any `HOST_PEER_HOST_SOURCE_RSP` section `era_wire_contract.md` does not list as open | wire eligibility |
| every HOST-PEER cell whose data another lane of that relation already moves — RGB in source-push, INPUT in *both* directions, ACTIVITY in *both* directions, visual baseline in source-push | each compile-asserted beside its eligibility entry in `era_split_wire_protocol.h`. That relation is one pipeline: the PEER never resolves keycodes, so the HOST's composed rows already carry a PEER-held layer key and the PEER's hits reach the HOST as projected matrix rows; the PEER renders the HOST's RGB config, so the response direction is the relation's only RGB carrier; the HOST's tapping engine sees every key of both halves as a local event, so a counter image would be a second carrier for facts the engine holds first-hand. DUAL-HOST cells of those sections are the open exceptions above. AUTHORITY is not covered: it carries no input and no render state |
| per-frame RGB event transport | closed |
| `EEPROM_SYNC` shape, direction, domain, or authority outside the exact contract in `era_host_peer_storage_contract.md` | class is in force; the tripwire is the surface |
| removed V16 `ATTEST`/`BEGIN`/`DATA`/`COMMIT`/`ABORT` storage semantics | closed |
| any writer of the storage summary response's relation time-anchor seat | reads zero **permanently**; the storage-op validator is that enforcement; the seat is reserved for nothing, because the `TIME_ANCHOR` section is the anchor's one carrier in every relation. Opening that section's DUAL-HOST cell did not open this seat (`era_host_peer_storage_contract.md`) |
| DUAL-HOST matrix, digest, and mirror execution | closed |
| a `DUAL_RUNTIME_BUNDLE` symbol or class `0x6x` id, **permanently** | stronger than "not yet": the design those ids encoded is refuted by shipped decisions, and nothing planned reintroduces them. The class carries no compiled id and no reservation; the classifier rejects it through its `default` arm (`era_split_wire_protocol.h`) |
| class `0x40` `ERROR_NACK` | no encoder writes it; nothing switches on the kind; `default` reject. Retired kind numbering is `era_identifier_map.md`'s |
| digest-only matrix response | closed |
| full row-array matrix payload | closed |
| bulk-page payloads other than the storage contract's `CHUNK_RSP` response and its `PUSH_CHUNK_REQ` request | closed |
| direct HID injection, remote HID endpoints, remote matrix replay, and full key-event replay | closed |

## Storage Lane Boundary

- Only the confirmed relation's initiator selects storage — the HOST-PEER PEER, or the DUAL-HOST Left.
- The responder responds only inside the admitted matching request slot.
- Transfer receipt is not durable completion. Only generation-matched core0 write/readback, runtime reload, Core1 restart, and relation revalidation may publish durable apply.
- Core1 never reads live QMK/VIA/EEPROM/RGB/matrix/USB/HID state and never writes EEPROM.

## Classifier Rule

Recognizing a reserved class so it can be rejected safely is not route,
admission, response, or execution authority.

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

The eligibility table is the proof in both directions: if its bytes read the same before and after, no surface moved.
