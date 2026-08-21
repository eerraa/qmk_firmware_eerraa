# ERA Closed Surface Contract

Status: active
Genre: contract
Canonical for: what is open on the split wire, what stays closed, and the
Opening Rule any opening must pay
Read when: editing classifiers, routes, scheduler domains, responder apply, or
any section's eligibility

## Currently Open Exceptions

**Which sections a relation may carry is a linked `const` eligibility table,
not a sentence here.** The planner ANDs its computed mask with this half's
entry and admission ANDs the received mask with the same entry, so a section
absent from the table can be neither sent nor accepted whatever the surrounding
code does. This document names what is open; the table is what enforces it, and
`era_performance_gates.md` reads the table's bytes. Marker values and the
cell-by-cell eligibility they index are `era_wire_contract.md`'s.

Every entry below shares one shape unless it says otherwise: it adds **no wire
operation, no class id, and no responder independent send**, and it rides the
existing `0x20`/`0x21` envelope pair in slots the relation already opens. The
list is exhaustive — a payload or behavior not named here is closed.

- **HOST-PEER source-push carries the packed matrix section and the AUTHORITY
  section, never both in one frame.** They do not fit one compact payload, so
  they ride separate routes and the layout walk refuses any frame claiming both
  (`era_wire_contract.md`).
- **AUTHORITY is open in every serviced relation and both directions.** It is
  the one section eligible everywhere, because it carries the relation's own
  revalidation rather than a payload the relation happens to carry — and it is
  what lets `SESSION_STATUS` stop running post-relation
  (`era_route_contract.md`). It is a precondition for that, not an addition on
  top of it, so closing it would re-open the `SESSION_STATUS` cadence.
- **`HOST_PEER_HOST_SOURCE_RSP` is response-slot only** and may carry only the
  sections `era_wire_contract.md` lists as open.
- **The replacement storage lane may execute only the exact
  initiator-initiated contract in `era_host_peer_storage_contract.md`** — that
  operation set and nothing beside it. Push is initiator-sent and responders
  answer only in admitted slots, so the lane opens no responder independent
  send.
- **DUAL-HOST storage is open.** The lane executes that same contract with
  `DUAL_HOST_LEFT` as initiator and `DUAL_HOST_RIGHT` as responder, on the same
  engine, the same operations, the same arbitration and the same wire roles. No
  `SESSION_STATUS` flag bit is involved.
- **Source supersession adds no payload and no direction.** Only the exact
  admitted `ABORT_RSP/SOURCE_CHANGED` alternate response is legal for
  `CHUNK_REQ`.
- **The `CHUNK_REQ` reserved bytes carry a 24-bit chunk-CRC hint**, and the
  zero-length `CHUNK_RSP` content-match form is admitted in the same admitted
  response slot. No new class or op value, and no new direction.
- **DUAL-HOST runtime is open per section, not per family.** The relation
  carries the INPUT layer section in both directions. The Right answers only in
  slots the Left opens; the constant cadence changes only how often the Left
  opens one, and which core decides when.

  **A responder answers that slot while storage is exclusive and carries
  nothing when it does.** The response plan is empty for the duration of the
  transfer, so the answer is the section-less ACK: the exception is not widened
  by the answer, and the surface a busy responder opens is the same one an idle
  responder opens with no section due.
- **The relation time-anchor section is open in both relations** — the HSRSP
  section `era_wire_contract.md` defines, inside already-admitted response
  slots, and one existing section marker in one existing DUAL-HOST response
  cell. No new body and no new deferral decision: the anchor keeps yielding to
  everything else in the section order (`era_route_contract.md`).
- **DUAL-HOST RGB-state is open in both cells.** The push cell exists on the
  INPUT section's both-directions precedent, because the response cell alone
  carries only the responder's changes. The body is the response direction's
  RGB-state body unchanged; in DUAL-HOST the sleep bit is zero at capture and
  skipped at apply, and the runtime effect is policy-gated
  (`era_authority_contract.md`).
- **The DUAL-HOST ACTIVITY section is open in both cells**, and adds no service
  reason. Its response marker is reused from a retired section; the reuse and
  its accepted capture ambiguity are recorded in `era_wire_contract.md`. The
  body carries a derived judgment-window flag and key-input counters plus a
  shared-clock instant — no key identity, no matrix, nothing that enters the
  HID path — and the window derivation from the tapping bridge's off-by-default
  options is what keeps a fresh-defaults image from ever making the section
  due.
- **The DUAL-HOST visual pressed-baseline is open in both cells**, under the
  RGB policy gate, with no new body: one body of one width crosses in both
  cells, the HOST-PEER response direction's reason-plus-baseline form
  unchanged.
- **The storage news section is open in both relations** — one existing section
  marker in one existing response cell, no new body. The news value is
  advertised only inside already-admitted HSRSP response slots, and probe
  scheduling reuses the exact existing storage operations. Its DUAL-HOST cell
  is what closed the last core0-originated periodic frame in a serviced
  relation.

  **Its all-clear body is an accepted-surface widening rather than a new
  section, and it is the one item in this list that paid the Opening Rule for a
  widening rather than an opening.** The news body may carry zero, and the decoder used to
  reject that value
  outright, so this admits a frame the classifier previously refused. Zero used
  to reach the wire as *absence*, which an edge-caching receiver cannot
  represent, so the value had no way across; the news value is latest-state and
  edge-armed like its siblings, and its transition to zero is what closes an
  episode (`era_wire_contract.md`). Nothing else about the body widened at
  that first opening: the section, its one-byte width and both eligibility
  cells are untouched. **There is no reserved-bit reject on this byte**
  (entry symmetry): bit7 is the responder's storage-pending fact — the
  response direction's twin of the push `STORAGE_PENDING` section, placed
  in this body because the response mask has no free marker — so the byte
  now admits its full width, bits 0..6 the news value and bit7 the pending
  flag. That second widening paid the Opening Rule again: contracts,
  source, build gates, with the paired-rise falsifier owed as compact
  device evidence.
- **The storage-pending section is open in the push cell of both relations**
  (the indicator redesign — the opening paid the Opening Rule in
  full: contracts, source, eligibility-byte proof, build gates, with the
  device falsifier owed as compact evidence). One new marker (`0x40`, never
  assigned in the push id space), one one-byte body, no new wire operation,
  no class id, no responder independent send, riding the standing exchange's
  existing `RUNTIME_SECTION_PUSH` reason with no new cadence. It carries the
  one fact the responder cannot derive — the initiator's storage relation
  still holds unfinished pair work — whose absence the EEPROM SYNC lamp used
  to pad with a fixed trailing bridge. Under entry symmetry the same fact
  flows in both directions: the response carrier is
  `STORAGE_NEWS` bit7, recorded as that section's second widening above —
  the response mask stays full, which is exactly why the reverse fact rides
  an existing body rather than a marker of its own
  (`era_wire_contract.md` for both bodies,
  `era_host_peer_storage_contract.md`'s **Diagnostics** for the fact and its
  consumer).

- **The restart arm is open in the push cell of both relations** — one new
  marker (`0x80`, never assigned in the push id space, the same reading of that
  rule `STORAGE_PENDING` took for `0x40`), one five-byte body, no new wire
  operation, no class id, no responder independent send, riding the standing
  exchange's existing `RUNTIME_SECTION_PUSH` reason with no new cadence. Push is
  the initiator's direction and the initiator owns the commit deadline, which is
  why the cell is where it is.

  **The section carries an act, not a feature**, and that is what this cell is
  open for. The link switch was its first user and the EEPROM clean is its
  second; a third would be a new act value in an existing body, not a new
  opening. What the cell may never carry is an act whose consequence is anything
  but "prepare, then reset this half" — a wire that can make a peer do arbitrary
  work is a different surface from a wire that can make it restart.
- **The restart intent is open in the AUTHORITY body in both relations and both
  directions**, as bits 3..7 of that section's flags byte: a widening of an
  existing body rather than a new section, the `STORAGE_NEWS` bit7 precedent
  applied for the same reason — **the response section mask has all eight
  markers assigned, so the responder's half of this agreement has no marker to
  take.** No new body, no new marker, no mask widening, and no change to any
  eligibility cell. AUTHORITY is the carrier because it is the one section
  eligible everywhere, never defers, and is gated by no sync policy bit; an
  agreement that could be turned off by a lighting preference would be a
  feature the owner cannot reach when they need it
  (`era_wire_contract.md` for both bodies).

  **This spends the flags byte's last reserved bit**, which the same contract
  records with the three alternatives that would buy another and what each
  costs. The byte joins `STORAGE_NEWS` in having none left.
- **A wire-triggered peer erase and reset is open**, and it is the widest of
  the three because no path in this tree resets the peer over the wire at all
  otherwise. It is scoped to the two agreed acts and to nothing else: the arm
  says which, the validator refuses any other value, and the act's own work is
  a dispatch on the receiving half rather than anything the frame describes.

  These three pay the Opening Rule in full: contracts, source, the
  eligibility-byte proof, the build and performance gates, with device evidence
  owed because hardware behaviour changes.

These exceptions do not authorize responder independent send.

## Closed Until Explicitly Opened

- responder independent send, including responder-owned storage data. The
  prohibition is on the *role*, not on one relation's name for it: the
  responder is the HOST-PEER HOST or the DUAL-HOST Right;
- any `HOST_PEER_HOST_SOURCE_RSP` section `era_wire_contract.md` does not
  list as open;
- **every HOST-PEER cell whose data another lane of that relation already
  moves** — RGB in source-push, INPUT in *both* directions, ACTIVITY in *both*
  directions, and the visual baseline in source-push. That relation is one
  pipeline: its PEER never resolves keycodes, so the HOST's composed rows
  already carry a PEER-held layer key and the PEER's hits reach the HOST as
  projected matrix rows; the PEER renders the HOST's RGB config, so the
  response direction is the relation's only RGB carrier; and the HOST's tapping
  engine sees every key of both halves as a local event, so a counter image
  would be a second carrier for facts the engine holds first-hand. **Each of
  these closures is compile-asserted beside its eligibility entry**
  (`era_split_wire_protocol.h`), which is what keeps a marker's mere existence
  from opening a cell. The DUAL-HOST cells of those same sections are the open
  exceptions above, not oversights in this line. AUTHORITY is not covered by
  this clause in either relation: it carries no input and no render state, and
  its openness is the exception above;
- per-frame RGB event transport;
- `EEPROM_SYNC` shape, direction, domain, or authority outside the exact
  contract in `era_host_peer_storage_contract.md`;
- removed V16 `ATTEST/BEGIN/DATA/COMMIT/ABORT` storage semantics;
- any writer of the storage summary response's relation time-anchor seat. It
  reads zero **permanently** and the storage-op validator is that enforcement;
  the seat is reserved for nothing, because the `TIME_ANCHOR` section is the
  anchor's one carrier in every relation. Opening that section's DUAL-HOST cell
  did not open this seat (`era_host_peer_storage_contract.md`);
- DUAL-HOST matrix, digest, and mirror execution;
- a `DUAL_RUNTIME_BUNDLE` symbol or class `0x6x` id, **permanently**. This is
  a stronger claim than "not yet" and it costs nothing to keep: the design
  those ids encoded is refuted by shipped decisions, and nothing planned
  reintroduces them. The class carries no compiled id and no reservation, and
  the classifier rejects it through its `default` arm;
- class `0x40` `ERROR_NACK`. No encoder in this tree ever writes it and nothing
  switches on the kind it produced, so it falls to the `default` reject like
  any other unmatched class. Its retired payload-kind value is not reused —
  retired kind numbering is `era_identifier_map.md`'s;
- digest-only matrix response;
- full row-array matrix payload;
- bulk-page payloads other than the storage contract's `CHUNK_RSP` response and
  its `PUSH_CHUNK_REQ` request;
- direct HID injection, remote HID endpoints, remote matrix replay, and full
  key-event replay.

## Storage Lane Boundary

- Only the confirmed relation's initiator selects storage — the HOST-PEER PEER,
  or the DUAL-HOST Left.
- The responder responds only inside the admitted matching request slot.
- Transfer receipt is not durable completion. Only generation-matched core0
  write/readback, runtime reload, Core1 restart, and relation revalidation may
  publish durable apply.
- Core1 never reads live QMK/VIA/EEPROM/RGB/matrix/USB/HID state and never
  writes EEPROM.

## Classifier Rule

Recognizing a reserved class so it can be rejected safely is not route,
admission, response, or execution authority.

## Opening Rule

Opening any closed item requires, in the same logical change:

- update of this contract and every affected authority/wire/route/domain
  contract;
- source implementation;
- source and ELF proof that unrelated surfaces remain closed;
- applicable build and performance gates;
- compact real-device evidence when hardware behavior changes.

Naming a later slice never opens its runtime surface.

Two kinds of change read like an opening and are not. Neither pays this rule,
and both are recorded here because misreading them is the common error:

- **a change of carrier.** Which core plans a section, and which route or which
  answer carries it, is a scheduling change inside surfaces already open. The
  surface is the frame, the marker, the body and the eligibility cell — not the
  lane they travel on.
- **a narrowing.** Refusing what was previously admitted — a section an answer
  may no longer carry, a class the classifier now drops to its `default`
  reject — shrinks the accepted surface. It is still wire-visible and still
  owes its gates, but this rule fires on opening.

The eligibility table is the proof in both directions: if its bytes read the
same before and after, no surface moved.
