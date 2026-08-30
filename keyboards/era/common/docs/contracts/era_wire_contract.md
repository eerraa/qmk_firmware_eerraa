# ERA Wire Contract

Genre: contract
Canonical for: compact wire payload ids and packet shapes, closed ids, section marker tables and body layouts, section eligibility, SESSION_STATUS frame validity, the section disciplines and the deferral order

## Compact Payload Kinds

| Payload | Wire shape | Current status |
| --- | --- | --- |
| `GRANT_ACK` | 1-byte compact control | open |
| `SESSION_STATUS` | compact control + status body | open |
| `HOST_PEER_SOURCE_PUSH` | compact control + class/op + section mask + bodies | open |
| `HOST_PEER_HEARTBEAT` | 1-byte compact control by route context | open |
| `HOST_PEER_ACK_STATUS` | 1-byte compact control by route context | open |
| `HOST_PEER_HOST_SOURCE_RSP` | class/op response | response-slot only; the zero-section envelope plus the eight-section set below |
| `EEPROM_SYNC` | replacement class `0xE0`; compact control plus one admitted bulk response kind | in force; PEER request/HOST response-slot only |

## HOST-PEER Class

```text
HOST_PEER_CLASS = 0x20

0x20: HOST_PEER_SOURCE_PUSH
0x21: HOST_PEER_HOST_SOURCE_RSP
0x22: unassigned
0x23..0x2F: reserved
```

Both ops are **relation-neutral**: the same envelope pair carries HOST-PEER and
DUAL-HOST runtime sections, and which sections a relation may carry is the
eligibility table's question, not the op id's. The `HOST_PEER` in these names is
historical in exactly the sense `era_source_map.md` already records for the
storage engine, and **renaming a proven envelope was declined as churn rather
than taken as a correction.** Class `0x6x` carries no reservation and nothing
planned reintroduces it.

`HOST_PEER_HEARTBEAT` and `HOST_PEER_ACK_STATUS` MUST NOT consume class/op ids.

The classifier admits only the operations it matches explicitly and rejects the
rest through its `default` arm, so `0x22` and the reserved window are rejected
for the same reason as any other unmatched id. No declarative constant names
that window, and nothing reads one — the `default` arm is what performs the
rejection.

## HOST-PEER Replacement Storage Class

The complete body layout, domain table, state machine, capacity, retry, and
durable apply rules live in `era_host_peer_storage_contract.md`.

```text
EEPROM_SYNC_CLASS = 0xE0

0xE0: PROBE_REQ       compact PEER request
0xE1: PROOF_RSP       compact admitted HOST response
0xE2: CHUNK_REQ       compact PEER request
0xE3: CHUNK_RSP       bulk-page admitted HOST response
0xE4: APPLY_REQ       compact PEER request
0xE5: APPLY_RSP       compact admitted HOST response
0xE6: COMPLETE_REQ    compact PEER request
0xE7: CLOSE_RSP       compact admitted HOST response
0xE8: ABORT_REQ       compact PEER request
0xE9: ABORT_RSP       compact admitted HOST response
0xEA: SYNC_STATUS_REQ compact PEER request (arbitration; summary or per-conflict)
0xEB: SYNC_STATUS_RSP compact admitted HOST response
0xEC: PUSH_CHUNK_REQ  bulk-page PEER request (the one initiator-sent bulk frame)
0xED: PUSH_RSP        compact admitted HOST response
0xEE: PUSH_CTL_REQ    compact PEER request (phases open/apply/complete/abort)
0xEF: reserved
```

Rules:

- Every compact storage body is exactly 15 bytes. The common identity prefix
  is control, operation, domain, schema, and 16-bit storage transaction
  generation.
- `SYNC_STATUS_REQ/RSP` carry the arbitration inputs in two forms selected
  by the domain byte: domain `0xFF` is the whole-family summary (the
  sender's per-domain changed mask and baseline validity), a real domain id
  is the per-conflict form adding the 16-bit divergence counter. The
  summary response's bytes 8..11 are the relation time-anchor seat, and the
  validator REQUIRES them **permanently** zero: the `TIME_ANCHOR` section is
  the anchor's one carrier in every relation, and this seat is reserved for
  nothing.
- `PUSH_CHUNK_REQ` is the one initiator-sent bulk-page frame, mirroring the
  `CHUNK_RSP` prefix (push source revision, chunk id, 1..252 data bytes —
  it has no zero-length form; no delta-hint mirror is open). `PUSH_CTL_REQ`
  carries the push phases (open/apply/complete/abort) in byte 6 with the
  push source revision and, outside abort, the episode's full-image CRC32;
  `PUSH_RSP` answers every push request in the `APPLY_RSP` shape, reusing
  the fixed status table.
- **Both halves must run the same replacement-protocol revision.** This is the
  wire-level statement of the identical-image rule; the decision itself is
  canonical in `era_source_map.md`'s stored-data compatibility. A responder
  that predates an operation never answers it, and storage between mixed
  revisions degrades to the bounded slow-retry loop while typing stays
  untouched — but **discovery and liveness are not covered by that clause**,
  because the `SESSION_STATUS` reserved-bit check below makes a mixed-revision
  pair form no relation at all, each half running standalone on its own USB.
  Flash both halves together.
- `CHUNK_RSP` and `PUSH_CHUNK_REQ` are the only bulk-page storage frames,
  one per direction. Each payload is at most 264 bytes and carries copied
  immutable data bytes (0..252 for `CHUNK_RSP`, 1..252 for
  `PUSH_CHUNK_REQ`); the complete frame is at most 271 bytes. A zero-length
  `CHUNK_RSP` body is the content-match acknowledgement for a `CHUNK_REQ` that
  carried a nonzero 24-bit chunk-CRC hint in its bytes 12..14.
- `ABORT_RSP/SOURCE_CHANGED` is the sole alternate operation accepted for an
  admitted `CHUNK_REQ`. It is a compact terminal response bound to the same
  transaction/domain/schema/source revision when the pinned publication is no
  longer current. No other request accepts an alternate response operation.
- PEER sends every request. HOST may return a storage response only in the
  admitted response slot with matching ACK sequence. HOST independent storage
  send is forbidden.
- The removed V16 `ATTEST/BEGIN/DATA/COMMIT/ABORT` interpretation is not a
  compatibility surface. Reusing its numeric class does not restore its
  manifest, source-election, epoch, or progress meanings.
- Compact CRC8, bulk-frame CRC32, full-image CRC32, and source revision serve
  the distinct integrity/freshness roles defined by the storage contract.
- `SESSION_STATUS.bulk_page_supported` mirrors
  `ERA_HOST_PEER_STORAGE_V1_ENABLE` at compile time; what that macro must
  compile in before the flag may be set is canonical in
  `era_host_peer_storage_contract.md`. That macro is **derived from
  `ERA_SPLIT_EEPROM_SYNC_ENABLE`** rather than chosen per board: a board that
  compiles the EEPROM sync feature gets the storage engine, and enabling the
  engine without the feature is a make `$(error)`
  (`split/era_split_qmk_rules.mk`). It is a capability
  fact, not route authority.
- Runtime classification accepts only the exact compact forms `0xE0..0xE2`,
  `0xE4..0xEB`, `0xED` and `0xEE`, and the exact `CHUNK_RSP` and
  `PUSH_CHUNK_REQ` bulk forms — the two bulk ops have no compact form and the
  eleven compact ops have no bulk form, which is the table above read as a
  validator (`split/communication_core/era_split_communication_core_storage.c`,
  reached from `era_split_wire_classify_payload()` in
  `split/era_split_wire_payload.c`). Mode/policy/pin/generation admission is
  still required before Core1 executes a request or response.

## `SESSION_STATUS`

Carries the accepted HOST/no-HOST flags, `matrix_ready`, `bulk_page_supported`
(true only where `ERA_HOST_PEER_STORAGE_V1_ENABLE` is compiled in), `usb_epoch`,
the host open/close generations, and the response-request bit.

Flags byte:

```text
0x01 accepted_host_open      0x10 status_response_requested
0x02 accepted_no_host        0x20 retired (was dual_host_ready)
0x04 retired (was storage changed hint)   0x40 matrix_ready
0x08 unassigned              0x80 bulk_page_supported
```

Validation, in this order, after the frame shape itself is refused unless
`payload_len == 9` and byte1 is `0x10`
(`era_split_wire_validate_session_status_payload()`,
`split/era_split_wire_payload.c`):

1. **Reserved-bit check.** The decoder refuses the **whole frame** for any bit
   outside `ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK`, which is exactly the set
   this revision's encoder can set. The three unassigned bits — `0x04`, `0x08`
   and `0x20` — are therefore reserved-and-rejected, not reserved-and-ignored.
2. **Role check**, `flags & 0x03 ∈ {0x01, 0x02}`: exactly one role bit.
3. **`matrix_ready` check.** The frame is refused when `0x40` is set without
   `0x02`, so `matrix_ready` may ride only a no-host half. When that half may
   set it at all is canonical in `era_authority_contract.md`'s **Matrix
   Ready**.

**The reserved-bit check exists because two carriers for one fact set may not
disagree about what a valid one is.** The AUTHORITY section carries the same
facts on the relation's own lane and refused every bit it had no fact for from
the day it opened; leaving these bits open here bought nothing but that
disagreement, since both halves always run the identical image
(`era_source_map.md`'s stored-data compatibility). That section has since spent
its last reserved bit (2026-08-19, below), so reserved-zero is now this frame's
rule alone and the shared form of it is the wider one — each carrier refuses
whatever it has no fact for. A future flag bit therefore costs exactly one line in
`ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK` — and, because rejection is
whole-frame, costs a half that predates it every fact this frame carries, which
is the mixed-image consequence stated at the storage class above.

**Reassigning `0x20` stays banned.** It was `dual_host_ready`; a
`SESSION_STATUS` that sets it dates the image that sent it, and re-taking the
value would make one captured byte ambiguous between two eras. A frame setting
`0x08` likewise dates the image that sent it. Both are rules about reading
captures, independent of what a current decoder does with the frame.

What `SESSION_STATUS` is used for — bootstrap, discovery, policy-generation
revalidation, liveness, and recovery — is canonical in
`era_route_contract.md`, as is the fact that no core0-originated periodic frame
runs inside a serviced relation. The storage-changed fact both relations once
carried in a flag bit rides the `STORAGE_NEWS` section (`0x40`) of the response
envelope below.

## `HOST_PEER_SOURCE_PUSH`

Current payload:

```text
byte0: compact control with extension bit
byte1: HOST_PEER_SOURCE_PUSH
byte2: section mask
byte3..: section bodies in ascending marker-bit order
```

Sections:

```text
0x01: MATRIX           packed half matrix   7 bytes   HOST-PEER
0x02: INPUT_LAYER      layer_state          1 byte    DUAL-HOST
0x04: AUTHORITY        session facts        7 bytes   both relations
0x08: RGB_STATE        config + sleep(=0)   7 bytes   DUAL-HOST
0x10: ACTIVITY         window + counters    11 bytes  DUAL-HOST
0x20: VISUAL           reason + baseline    8 bytes   DUAL-HOST
0x40: STORAGE_PENDING  pending flag         1 byte    both relations
0x80: RESTART_ARM      act + param/phase + when  5 bytes  both relations
```

Storage-pending section body:

```text
byte2 bit6: storage-pending section marker
body byte0: bit0 = the initiator's storage relation still holds unfinished
            pair work (settled divergence, decided cells, or a
            content-moving episode not yet closed)
            bits 1..7 reserved zero, validator-refused
```

It exists because the responder structurally cannot derive this fact —
every queue, cell and completion poll is initiator-side state — and the
EEPROM SYNC lamp's fixed trailing bridge was a clock standing in for it.
The fact's composition and its consumer (the lamp's mirror arm) are
canonical in `era_host_peer_storage_contract.md`'s **Diagnostics**; this
section owns the encoding above and nothing else. It is the one push
section carried on the **INPUT-class discipline's forced first cross**: the
receiver holds the value applied, so an invalid sent shadow forces the
current value — zero included — across once per relation, retiring the
mirror through the apply path rather than stranding it. This section flows
initiator→responder and carries the initiator's copy of the fact; the
responder's copy crosses as bit7 of the `STORAGE_NEWS` response byte
(entry symmetry — the body below), because the response mask
is full and a twin marker does not exist to take. **The summary and the
news value do not stand in for that bit**: they carry the responder's
*settled* phase only, so without it the responder's dirty phase is
invisible to the initiator until the 1000 ms quiet closes, and an R-side
layout load reads as a several-second late join.

Restart arm section body:

```text
byte2 bit7: restart arm section marker
body byte0: bits0..1 = wire param (act-defined value or phase below)
            bits2..3 = act (0 idle, 1 link speed, 2 EEPROM clean; 3 refused)
            bits4..7 reserved zero, validator-refused
body byte1..4: T_commit, sync-timer milliseconds, little-endian
```

The initiator's carrier for the preparation and commit phases of the same
restart act on both halves, and the only half that has a section: the response
mask has all eight markers assigned, so the responder's request, PREPARED and
COMMIT_ARMED states ride the AUTHORITY flags byte instead (below). `0x80` had
never been assigned in the push id space, which is the same reading of the
marker rule `STORAGE_PENDING` took for `0x40` while the response direction
already used that value.

**The section is the mechanism, not the feature.** It carried the link switch
alone when it opened and now carries any agreed act; the body says which, and
the acts are `split/era_split_restart_agreement.h`'s. That generality is what
keeps the push id space from having to find a second marker for the second
thing two halves must perform at a shared deadline, and there is no second
marker to find.

**The same two-bit wire-param seat remains act-defined; CLEAN spends values on
agreement phase rather than changing its user/API parameter.** The complete
section validation table is:

| Act | wire param | `T_commit` | Meaning |
| --- | ---: | ---: | --- |
| idle | 0 | 0 | canonical all-zero body |
| link speed | the validated link-level parameter | nonzero | the existing shared-deadline arm |
| EEPROM CLEAN | 1 | 0 | `PREPARE`; no deadline exists |
| EEPROM CLEAN | 2 | nonzero | `COMMIT`; the shared deadline |

For CLEAN, wire-param 0 or 3 in this section is malformed. Act value 3 remains
refused in every form. CLEAN's application parameter remains zero and the act
table's `param_max` remains zero; values 1 and 2 never reach the local CLEAN
dispatch as an application argument.

The CLEAN ordering is load-bearing. `PREPARE` asks the pair to enter storage
quarantine and perform the local reboot-durable boot-magic invalidation; its zero
timestamp cannot be interpreted as a deadline. Only after the initiator holds
both local and peer PREPARED for the same relation may it publish `COMMIT`.
The responder adopts that nonzero deadline and advertises COMMIT_ARMED through
AUTHORITY; the initiator takes the same local deadline only after consuming
that state. A response snapshot may have been published before the incoming
push was applied, so transaction success or a PREPARED echo is not a
COMMIT_ARMED confirmation; the initiator waits for the matching later
latest-state edge.

**A nonzero deadline is absolute and not a countdown**, and that is what the
four bytes buy. The COMMIT section can cross again — a forced refresh, a reopen
— and an absolute deadline is idempotent under re-delivery where a countdown
would restart itself. It is the `TIME_ANCHOR` section's encoding on the same
clock, and the responder is that clock's source in both relations, so the
initiator is quoting a time its peer owns. PREPARE is the one admitted
zero-deadline live form and validators distinguish it by the CLEAN/param-1
tuple.

**The section forces its current body to cross once per relation.** Relation
rotation invalidates the standing sender shadow, so ordinary idle state crosses
as the canonical all-zero body; a confirmed live arm instead re-crosses its
same absolute deadline. The force is load-bearing for an unconfirmed CLEAN
COMMIT: rotation retires that proposal but holds the section at idle until the
peer has processed the disarm and advertised a fresh PREPARED state, and only
then may a replacement deadline be created. A previous relation's PREPARED or
COMMIT_ARMED state is never accepted as current in a new one. The local CLEAN
quarantine and monotonic prepare obligation survive the identity change and may
be discarded only through the controlled reset defined in
`era_host_peer_storage_contract.md`; a fresh boot publishes only the idle body.

**It is the yielding class's first claimant rather than a member of the
never-deferring core**, because five bytes do not fit beside that core in
either relation (`3 + 1 + 7 + 1 + 5` in DUAL-HOST and `3 + 7 + 1 + 5` in
HOST-PEER, both past the fifteen-byte budget). It claims ahead of ACTIVITY: a
handshake that has started gates a bounded commit where a judgment refresh does
not, and a frame can carry the five-byte phase beside the seven-byte AUTHORITY
body at exactly fifteen bytes. That fit permits the answer and phase to share a
frame when both were already published; it does not promise that applying an
incoming phase mutates the response snapshot in the same exchange.

`VISUAL` is the visual pressed-baseline twin in the push direction, at the same
fixed width the response body uses. Render state whose content is pressed
positions: consumed by the receiving half's hit tracker only, it writes no
matrix state and reaches no action processing (`era_invariants.md`'s
render-state clause). Both directions ride the RGB policy bit's sender/receiver arms; the
reason is core1's, derived from its own sent shadow (`RELATION_OPEN` once per
relation/reopen, then `RENDER_RESET`, the ordinary diff).

`RGB_STATE` carries the response direction's RGB-state body byte for byte, on
the INPUT section's both-directions precedent: the response cell alone carries
only the responder's changes, so "either half" requires both cells. One
relation rule: in DUAL-HOST the sleep bit is zero at capture and skipped at
apply, in both directions (the sleep-intent rule below).

**`MATRIX` and `AUTHORITY` are both eligible in HOST-PEER and are never
planned in one frame.** `MATRIX` is `ERA_SPLIT_WIRE_HALF_MATRIX_BYTES`, which
is a board fact — seven on the nine-column boards, nine on `sirind/tomak` —
and `AUTHORITY` is seven, so `3 + 7 + 7` is already two over the compact budget
and the wider board is further over. They ride separate frames: the
matrix keeps core0's `HOST_PEER_MATRIX_SOURCE_PUSH` route and authority rides
the standing exchange, which core1 stamps `RUNTIME_SECTION_PUSH`
(`era_route_contract.md`). The separation is a budget fact rather than a
scheduling one, so no planner has to be trusted with it: a mask claiming both
sums past any legal `payload_len` and the layout walk refuses the frame as
malformed rather than truncating it.

Rules:

- `byte2` is a section mask, not a flags byte. The validator walks it, sums
  the declared body lengths, and MUST land exactly on `payload_len`. An
  unassigned marker bit is a reserved-bit rejection.

  > **REFUSED:** restore the layout walk's variable-length machinery.
  > **WHY:** its bound is the `payload_len != 3 + fixed_total` arm, which
  > the parameter selected around rather than provided — the machinery
  > never carried the check it appeared to.
  > **REOPENS:** a section whose body length is genuinely variable.
- A zero mask is invalid here. The section-less request form is the one-byte
  compact control payload, so an empty envelope is a malformed frame rather
  than an empty one. The response direction is the opposite case: `byte2 ==
  0x00` is its no-section envelope and stays valid.
- **`MATRIX` is HOST-PEER only, and `INPUT_LAYER` and `RGB_STATE` are
  DUAL-HOST only, and that is enforced by the eligibility table rather than by
  this sentence.** Which sections a relation may send and accept is one linked
  `const` table read at one send site and one admission site per direction, so
  a section absent from it can be neither sent nor accepted. The gates read
  the table's bytes; see `era_performance_gates.md`.
- **`STORAGE_PENDING` is eligible in both relations' push cells** — the
  initiator is the HOST-PEER PEER or the DUAL-HOST Left, and this cell
  carries the initiator's copy of the fact; the responder's copy rides
  `STORAGE_NEWS` bit7 in the response direction (entry symmetry).
  It joins the never-deferring one-byte facts: the
  push-direction core (`3 + INPUT_LAYER + AUTHORITY + STORAGE_PENDING` in
  DUAL-HOST, `3 + AUTHORITY + STORAGE_PENDING` in HOST-PEER, the matrix on
  its own route) fits one compact frame by `_Static_assert`, and ACTIVITY
  keeps the accepted shape of fitting beside each push-direction one-byte
  fact but not both at once, deferring one poll when both change on its
  drain poll.
- Full row-array matrix is forbidden.
- Digest-only matrix response is forbidden.
- RGB is forbidden here in HOST-PEER, and a compile assert beside the
  eligibility entry keeps it closed now that the marker exists: the PEER is
  dark and renders the HOST's config, so the response direction is that
  relation's only RGB carrier and a push cell would be a second carrier for
  data the response already moves. INPUT is forbidden in HOST-PEER: that
  relation's PEER never resolves keycodes, so the HOST's composed rows already
  carry a PEER-held layer key. **ACTIVITY is closed in both HOST-PEER cells**,
  push and response, with stays-closed asserts, for the same structural reason:
  that relation is one pipeline, so the HOST's tapping engine already sees
  every key of both halves as a local event and a counter image of the same
  facts would carry nothing.
- EEPROM DATA is forbidden.

## `HOST_PEER_HOST_SOURCE_RSP`

Active envelope shape:

```text
byte0: compact control with extension bit
byte1: HOST_PEER_HOST_SOURCE_RSP
byte2: section mask
byte3..: one body per set marker, in ascending marker-bit order
```

Active sections:

`byte2` is a plain 8-bit section mask. The marker values are compiled in
`era_split_wire_protocol.h`:

```text
0x01: INPUT layer     layer_state          1 byte    DUAL-HOST
0x02: ACTIVITY        window + counters    11 bytes  DUAL-HOST (reused marker)
0x04: AUTHORITY       session facts        7 bytes   both relations
0x08: lock-state      lock LEDs            1 byte    HOST-PEER
0x10: visual-resync   reason + baseline    8         both relations (RGB-policy-gated)
0x20: RGB-state       config + sleep       7         both relations
0x40: storage news                         1         both relations
0x80: relation time anchor                 4         both relations
```

**Every section in both id spaces is latest-state and edge-armed.** A section is
advertised while its live value differs from what the wire last confirmed,
retired when the wire confirms it, and runs on no timer. Both sent-state shadows
drop on the relation identity rotation, because the peer clears what it holds on
that same event and a surviving shadow would leave a reopened peer at zero. A
sent shadow advances from the wire's own section byte, so a section cannot
retire without having crossed. The sections below state only their additions to
this.

**Lock-state (`0x08`) is HOST-PEER-only by structure, not by omission**: in
DUAL-HOST each half is USB-enumerated and receives its own host's LED report
directly, so there is no lock fact to carry that the receiving half does not
already hold. A DUAL-HOST capture showing a lock desync this section would have
carried is what would say this reason is wrong.

**A new marker takes a value never assigned in either direction**, so that a
`sec=` value in a capture is never ambiguous between two eras — the same rule
the flags byte's `0x20` ban serves. Where reuse is taken anyway it is an
accepted cost, recorded at the section that takes it (ACTIVITY's `0x02`,
below).

`byte2 == 0x00` is the no-section envelope. **Visual-resync combined with
RGB-state is not a valid combination at all, in either direction**: a visual
section is always reason plus the full half-matrix baseline, which is the same
board fact `MATRIX` is — eight bytes on the nine-column boards, ten on
`sirind/tomak` — and `8 + 7` is already fifteen body bytes
against the twelve the compact response leaves, so no legal `payload_len`
exists for the pair and the mask walk refuses the frame as malformed rather
than truncating it. The send side never plans both either, because the
responder snapshot's send-clip drops RGB when the visual section is present.
Section bodies follow `byte2` in ascending marker-bit order, and the same
eligibility table that governs the request direction governs this one.

Lock-state section body:

```text
byte2 bit3: lock-state section marker
body byte0: HOST-known lock LEDs, bits 0..2
            bit0 = Num Lock, bit1 = Caps Lock, bit2 = Scroll Lock
            bits 3..7 reserved zero
```

INPUT layer section body:

```text
byte2 bit0: INPUT layer section marker
body byte0: the sending half's layer_state
```

AUTHORITY section body, identical in both directions:

```text
byte2 bit2 (request byte2 bit2): AUTHORITY section marker
body byte0:   flags
              bit0 = accepted_host_open
              bit1 = accepted_no_host
              bit2 = matrix_ready
              bits 3..4 = restart wire param (act-defined value or phase)
              bits 5..6 = restart act (0 idle, 1 link speed, 2 EEPROM clean;
                          3 refused)
              bit7 = restart state-qualified (`armed` for link speed;
                     PREPARED/COMMIT_ARMED qualifier for EEPROM CLEAN)
body byte1..2: usb_epoch, little-endian
body byte3..4: host_open_generation, little-endian
body byte5..6: host_close_generation, little-endian
```

It carries the session facts `SESSION_STATUS` carries minus
`bulk_page_supported`, plus this half's restart intent, which that frame does
not carry at all. `bulk_page_supported` is a compile-time capability that cannot
change inside a session (`era_authority_contract.md`), so it is learned once at
discovery, and **a half consuming this section MUST leave that field in its
peer cache untouched** rather than writing it from a frame that never held it.

The validator applies the rules the `SESSION_STATUS` validator applies to the
facts they share — exactly one role bit, and `matrix_ready` only on a no-host
half — shared literally, because **two carriers for one fact set may not
disagree about what a valid one is.** The two flag masks were never identical
and are not required to be: this section has no `bulk_page_supported` bit and
that frame has no restart bits, so each carrier refuses the bits it has no fact
for, and what is shared is the rules rather than one constant.

**Reserved bits zero used to be a third shared rule and is now `SESSION_STATUS`'s
alone, because this byte has no reserved bits.** Bit 7 was the last one and the
agreement's qualified state took it; the mask is `0xFF` and `era_split_wire_protocol.h`
asserts that it is. The rule is discharged rather than dropped — every field is
checked for what it may say, which is a stronger test than asking whether
anything is set that should not be. **The next fact on this section needs its
own home, not a hidden bit.**

> **REFUSED:** widen the response section mask to 16 bits to free a marker.
> **WHY:** it charges every section-carrying response frame a header byte
> forever, and the eight assigned markers include the `3 + visual(8) +
> anchor(4) = 15` combination the planner already relies on fitting.
> **REOPENS:** a fact that needs a section of its own and cannot ride an
> existing body, with a measurement that the extra header byte does not move
> the poll-rate budget.

> **REFUSED:** retire a response section to free its marker.
> **WHY:** all eight have live consumers; none is a compatibility remnant.
> **REOPENS:** a feature retirement that leaves one genuinely unread.

> **REFUSED:** widen the AUTHORITY body to eight bytes for a spare flags byte.
> **WHY:** `3 + 8 + 5 = 16` against a 15-byte compact budget, so the arm and
> the answer stop sharing a frame and every agreement costs an extra round
> trip.
> **REOPENS:** a compact budget wider than 15 bytes.

**Bits 3..7 are here because there was nowhere else.** The response mask has
all eight markers assigned, so the responder's half of the agreement has no
section of its own to take — the situation `STORAGE_NEWS` bit7 answered by
riding an existing body, and that byte has no bits left either. AUTHORITY is
what remains and is also what fits: it is the one section eligible in every
serviced relation and both directions, it never defers, no sync policy bit
gates it, and both ends already hold a shadow, so the intent gets change
delivery with no mechanism of its own.

**The five restart bits are one act-qualified state, not independent flags.**
The complete AUTHORITY validation table is:

| Act | wire param | bit7 | Meaning |
| --- | ---: | ---: | --- |
| idle | 0 | 0 | no restart state |
| link speed | validated link-level parameter | 0 | request |
| link speed | the same parameter | 1 | matching shared deadline adopted |
| EEPROM CLEAN | 0 | 0 | `REQUEST` |
| EEPROM CLEAN | 1 | 1 | local physical boot-replay proof complete: `PREPARED` |
| EEPROM CLEAN | 2 | 1 | CLEAN COMMIT deadline adopted: `COMMIT_ARMED` |

For CLEAN, `(param 1, bit7 0)`, `(param 2, bit7 0)`, `(param 0, bit7 1)` and
every param-3 form are malformed. The full tuple participates in the sender
shadow and receiver edge comparison, so REQUEST→PREPARED→COMMIT_ARMED cannot be
collapsed into one sticky armed level. Relation identity is part of the
consumer's acceptance even though it costs no byte in this body.

**A CLEAN request is published before either qualified state.** PREPARED says
only that this half's one-word prepare survived physical boot replay under
storage quarantine; it carries no deadline. COMMIT_ARMED says that this half adopted
the matching nonzero COMMIT deadline. Only that last state confirms an arm.
The separation prevents a stale PREPARED answer from being mistaken for the
commit echo and spends no new body, marker, operation or cadence.

**The restart intent is not a session fact, and the two consumers differ on
purpose.** `era_split_wire_authority_equal()` compares it, because that
function is both the sender's shadow and the receiver's edge, so an intent
change must cross and must be delivered.
`era_split_scheduler_session_note_peer_authority()`
(`split/era_split_scheduler_session.c`) ignores it, because a restart intent is
not a peer-session edge and raising one would put the mode planner on the
agreement's cadence.

The three 16-bit values are change detection and nothing else: no consumer
reads their magnitude, and what they buy is that a close-and-reopen inside one
poll period is still visible in a latest-state section.

**This section is what lets `SESSION_STATUS` stop running post-relation**
(`era_route_contract.md`) — a precondition for that, not a latency improvement
on top of it. It carries the relation's own revalidation and no payload the
receiving half renders or resolves, which is why it is the one section eligible
in every serviced relation.

**The INPUT layer body is one byte** because this board's layer count selects
an 8-bit `layer_state_t`. That is asserted rather than assumed, in
`era_split_peer_layer.c`, which is the unit that would truncate; a board with a
wider layer state fails there naming this constant instead of silently
truncating on the wire.

Every value of the byte is valid. A layer bit above the receiving half's layer
count is not a wire error — the composing half simply resolves no action on a
layer its keymap does not define.

ACTIVITY section body, identical in both id spaces (marker `0x02` here and
`0x10` in the push direction):

```text
byte2 bit1: ACTIVITY section marker
body byte0: flags — bit0 = judgment window open, bits 1..7 reserved zero
body byte1: press counter, mod 256
body byte2: release counter, mod 256
body byte3..6: last-press sync-timer milliseconds, little-endian
body byte7..10: last-release sync-timer milliseconds, little-endian
```

The sending half's tap-hold judgment window and its key-input activity, folded
into state so the judgment a single keyboard makes from events can be made
across two USB devices whose events never cross. **The window flag is derived,
never a policy bit**: it rises only while a tap-hold key is in flight whose
effective runtime options consume other-key input (`era_tapping`'s cached
bridge — permissive hold, hold on other key press, retro tapping, all default
off), so fresh defaults never open one and the section is never due — the
property the silence legs read. The activity fields are advertised live only
while the **peer's** window flag is up and stay frozen otherwise, so ordinary
typing with both windows down moves no wire byte.

**The counters are change detection and dedup; the event instants on the shared
clock are the judgment.** A counter delta alone cannot say whether its events
fell inside the window, because frozen fields un-freeze with a stale image when
a window opens, so every consumer orders by the timestamps. Both edges therefore
carry their own instant, and the permissive-hold pair is two stamped facts each
ordered by its own: a strike's press and release predominantly cross in one
image on this transport — the responder's fields stay frozen until the
initiator's window flag crosses, and its snapshot samples latest-state at
housekeeping cadence — so a rule admitting only releases that arrive in a later
image starves by construction. The shared discipline applies with one refinement
stated at the shadows in source: an invalid sent shadow compares against the
all-zero baseline rather than forcing a send, and the rotation drops the shadows
and the receiving cache together, so a reopened relation on fresh defaults still
crosses nothing.

**The marker `0x02` is reused, not new** (owner decision). It previously
carried a body-less "ask me" that a responder could send only inside an
already-open cadence window, retired once the initiator began polling
unconditionally. **The 16-bit-mask alternative was rejected as capacity rather
than completeness**: widening the mask charges every section-carrying response
frame one header byte forever and touches every validator, to buy spare bits
nothing scheduled needs. The capture ambiguity the reuse accepts is practically
empty, and the era boundary is recorded in `era_capture_reading.md`'s `sec=`
rules.

**Three owner decisions bound anything built on this seam.** The DUAL-HOST
gaming property — no added hop, each half reporting to its own host directly —
is a hard constraint, so a judgment crosses as state beside traffic that
already flows and never as an event routed through the other half. The
few-millisecond cross-half delivery skew is accepted as physical rather than a
defect to engineer away. And **arming is derived, not owned**: it comes from
the tapping bridge the user already chose, so the semantics they picked for one
half's keys are the semantics they get across the seam.

> **REFUSED:** symmetric event replication, or a both-hosted composed relation,
> so that both halves judge from one event stream.
> **WHY:** each adds the hop the DUAL-HOST gaming property forbids, and that
> property is the product rather than an implementation choice.
> **REOPENS:** permanent while DUAL-HOST means two direct per-half reports.

> **REFUSED:** an independent cross-half kill-switch for the family.
> **WHY:** arming is derived, so a separate control is a way for the seam to
> disagree with the same keyboard's own tap-hold semantics.
> **REOPENS:** a layout for which the derived arming is measurably wrong.

> **REFUSED:** cross-half chords — S3, the ladder's third rung.
> **WHY:** the section carries no key identity by design
> (`era_closed_surface_contract.md`), so a chord needs a wider body than any
> recorded demand pays for.
> **REOPENS:** a recorded demand for cross-half chords.

**Two approximations stand by design.** One stamp per edge: the body carries
the newest press and release instants only, so an image holding several events
is judged by its newest. And the delivery bound: a pair whose LT release beats
the roughly one-housekeeping delivery still taps, because no judge can use data
that has not arrived.

Visual-resync section body:

```text
byte2 bit4: visual-resync section marker
byte3: reason
       0 = relation open baseline
       1 = TX queue overflow                          (no sender)
       2 = visual tick gap / bounded forced refresh
       3 = relation reopen                            (no sender)
       4 = visual render reset / pressed baseline changed
byte4..byte(3 + N): packed HOST local-half pressed baseline
                   N = ERA_SPLIT_WIRE_HALF_MATRIX_BYTES
                   bit index = local row * MATRIX_COLS + col
                   unmapped or high reserved bits are zero
```

**The body is fixed at reason plus baseline in both directions, and there is no
reason-only form.** A capture whose visual section is one body byte is a
pre-2026-08-10 image.

**Reasons `1` and `3` have no sender, and select no different apply at the
receiver.** `TX_OVERFLOW` and `RELATION_REOPEN` are named by the initiator's
replay predicate — the test that decides whether a body replaces the receiver's
pressed set rather than diffing against it — but nothing in this tree assigns
them: the response planner picks `RELATION_OPEN`/`RENDER_RESET`/`TICK_GAP`, the
push encoder picks `RENDER_RESET`/`RELATION_OPEN`, so those two comparisons
would be runtime tests of a value the wire never carries. The predicate
therefore names only `RELATION_OPEN` and `TICK_GAP`. **The two values stay in
the table and stay accepted**: the layout validator admits any reason up to
`..._VISUAL_RESYNC_REASON_MAX`, so a frame carrying one is valid and applies as
a plain diff rather than a pressed replay. **Narrowing the validator to the
three producible values is the wire layer's own decision and is deliberately not
taken** — the rows are annotated rather than deleted.

RGB-state section body (identical in the push id space at marker `0x08`):

```text
byte2 bit5: RGB-state section marker
body byte0 flags:
           bit0 = RGB enabled
           bit1 = RGB sleep/suspend intent
           bits2..7 reserved zero
body byte1: RGB Matrix mode, bits6..7 reserved zero
body byte2: hue
body byte3: saturation
body byte4: value
body byte5: speed
body byte6: LED flags
```

**The sleep bit does not cross in DUAL-HOST.** It is a fact about the one USB
session a HOST-PEER pair shares, and DUAL-HOST halves each own one, so there a
half captures it as zero into every snapshot and plan it publishes and the apply
consumes configuration only — two guards, the same both-ends discipline the
accept clips use. The bit is HOST-PEER's, where the receiving PEER has no USB
session of its own.

**On the receiving side the apply publishes this bit and does not write the
render gate**: that gate has exactly one owner, the owner is a property of the
relation, and the wire is it only on a HOST-PEER PEER — canonical in
`era_authority_contract.md`'s **Lighting Sleep Ownership**. An apply that wrote
the gate directly is what let a demoted half's own stale sleep latch survive
underneath the wire's answer. **On the sending side the captured bit is the
HOST's resolved sleep decision and not its render gate**, so a board status
report that force-lights the HOST cannot flash the PEER.

If a both-halves-dark policy is ever ordered — "either host sleeps, both halves
dark" — it belongs in the sleep decision as policy and never in a wire apply,
which is where the bit already lives and would therefore let a peer's host
darken a half whose own host is awake.

Storage news section body:

```text
byte2 bit6: storage news section marker
body byte0: bits 0..6 = responder storage news value, 1..127 forward-only,
                        0 = nothing to claim
            bit7 = the responder's storage-pending fact (entry
                   symmetry) — the response direction's twin of the
                   push STORAGE_PENDING section
```

The section constant is
`ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS`; the carrier
fields are `host_source_storage_news[_valid]`, `peer_storage_news[_valid]` and
`send_storage_news`. `_VALUE_MASK` keeps its suffix because bits 0..6 really are
a mask, and `_FLAG_PENDING` (`0x80`) is bit7's constant. The flag bit was
reserved-zero-refused from this section's birth, so a capture carrying it set
unambiguously dates the image at or after the 2026-08-14 entry-symmetry
change — and with it the byte has no reserved bits left: every value is
valid, the news consumer masks the flag off itself, and the two facts in one
byte stay two facts on the console (`news=`/`pnews=` print the value bits;
the shim's `mir` is the flag's truth). The older spelling `SETTLED_DIRTY_MASK` survives at exactly one site,
the field note in
`communication_core/era_split_communication_core_standing.h`, so a grep for it
dates a patch or capture rather than locating a live carrier. The value's
meaning and maintenance are canonical in `era_host_peer_storage_contract.md`;
this section owns the encoding above and nothing else.

Relation time-anchor section body:

```text
byte2 bit7: relation time-anchor section marker
body byte0..3: HOST sync-timer milliseconds at snapshot publish,
               little-endian
```

When multiple section bodies are present they follow byte2 in ascending
marker-bit order: INPUT layer, then ACTIVITY, then AUTHORITY, then lock-state,
then visual-resync, then RGB-state, then storage news, then
relation time anchor. Encode order is the serializer's; priority is the
plan's, and the two deliberately differ for ACTIVITY, whose claim on the
budget runs ahead of the refreshes that follow it on the wire.

Valid payload lengths follow from the mask walk rather than an enumeration:
`3` plus the sum of the present sections' declared bodies, which must equal
`payload_len` exactly. Lock-state may share any valid HSRSP frame and costs one
body byte when it does. **RGB-state and visual-resync never share a frame, and
that is a property of the wire rather than a deferral rule** — the pair has no
legal `payload_len` at all (above), so whichever of the two loses the deferral
order below goes out on a later response slot. Every mask combination the wire
admits fits the compact budget, visual-resync with pressed baseline plus the
storage news byte included. The time anchor shares any slot whose remaining
compact budget fits its four bytes — including the full visual baseline on the
current seven-byte half-matrix boards, where `3 + 8 + 4` lands exactly on the
15-byte budget — and defers to a later response slot only when it does not fit.

Rules:

- `HOST_PEER_HOST_SOURCE_RSP` may be used only as the HOST response to an
  admitted HOST-PEER PEER heartbeat or source-push request with matching ACK
  sequence.
- HOST independent send remains forbidden.
- The relation time-anchor section is the transport time service: it carries
  the responder's sync-timer milliseconds — the responder is the time authority
  in both relations, the HOST in HOST-PEER and the Right in DUAL-HOST — and the
  initiator applies it through the one corrected setter so both halves share one
  monotonic timeline. **Anchor adoption is relation-scoped:** rotating the
  relation withdraws the initiator's permission to create a new shared-clock
  restart deadline until a TIME_ANCHOR from that new relation has actually been
  applied; the existing sync-timer value itself is not cleared. This prevents an
  initiator that outlives a sequential peer reboot from treating the old peer's
  uptime as proof of the new peer's epoch. **Both ends of the delivery are
  corrected.** At the receive
  end core1 stamps the receive instant beside the anchor and the setter adds the
  held time; at the send end core0 captures a `timerawl` stamp beside the
  sync-timer reading and core1 adds the elapsed at encode, so a snapshot serving
  a poll a period late does not age the anchor by that period — the value on the
  wire is the one the anchor would carry at send. That subtraction is its own
  instrument (`ahold`, `era_capture_reading.md`); what remains uncorrected is
  sub-millisecond wire transit. The section stays advertised until its
  sent-commit drains once per relation open/reopen — the rotation drops its sent
  shadow, which is DUAL-HOST's only reopen — then refreshes at a slow bounded
  cadence. RGB effect phase is its first consumer, and it carries no per-frame
  or reactive data.
- The storage news section carries the responder's RAM-only news value defined
  in `era_host_peer_storage_contract.md`, in any admitted HSRSP response slot.
  **It says "ask", and it names nothing**: the initiator arms a
  whole-family summary and derives every direction from that, so this section
  cannot bypass quiet deadlines, the `BUSY` pin handoff or generation
  allocation for the simple reason that it selects no domain. It adds no wire
  operation. A responder whose local EEPROM sync requested policy is disabled
  advertises `0`, which is a value rather than an absence — see below.

  **Zero is a legal body**, and the shared discipline applies with one addition
  the render sections do not need: a forced refresh re-advertises a **nonzero**
  value on a bounded period, because a lost response on this section costs a
  config edit rather than a frame of render state. Its period is the storage
  dirty-quiet interval, so a refresh can never outpace a real settle.

  **The refresh's gate is `value != 0`, which against a forward-only counter
  means "this half has settled a departure at least once" — the value is
  boot-scoped, survives relation rotations, and never becomes false again.**
  So after the first departed settle the section re-crosses **once per refresh
  period for the life of the relation**. The idle property is correspondingly
  wider since the at-agreement narrowing: a relation in which nothing has
  departed advertises nothing after its first cross, identical-content
  reloads included (`era_host_peer_storage_contract.md`).

  **The section forces one cross per relation open, every value included —
  zero too**, which arrived with bit7. The invalid sent shadow used to compare
  against the all-clear, on the reasoning that the never-falling counter made
  any fact worth re-stating nonzero by itself; bit7 broke that premise — the
  byte now falls to `0x00` at every operation's close, and the initiator's
  mirror survives identity rotations on the promise that the live carrier
  re-states itself on the fresh relation. A pure-target responder ends with
  news 0 and pending 0, so under the old rule its post-rotation byte never
  crossed and a mirror caught standing at the rotation stayed lit for good —
  device-caught on the first two-domain initiator-edited load. The force is
  the push `STORAGE_PENDING` twin's own discipline with the same recorded
  cost: one body byte once per reopen, silence afterwards.

  The repeat's cost is bounded and is not a second carrier: the initiator
  discards a value equal to the one it holds, so no summary is armed and no
  storage transaction follows; the section has no `rt` arm, so the runtime
  silence legs do not see it; the responder pays one snapshot republish per
  period. Whether it should stop once the initiator has demonstrably acted is an
  **open question**: the responder cannot observe what the initiator took, and narrowing it from
  source alone would trade a bounded repeat for an unbounded lost config edit.

  Like every section here it crosses once and on the standing answer, so an
  initiator never holds two ideas of what the responder last said — canonical in
  `era_route_contract.md`'s **One carrier for the response section set**.
- **A source-push answer carries no response section.** A request from the
  initiator's core0 lane is answered with the bare control ACK below. This
  narrows *when* a section crosses and never *whether* the relation may carry
  it: the eligibility table above is unchanged, which is what makes the linked
  table read the proof that nothing opened.
- While no section is due, the runtime response is the one-byte
  `HOST_PEER_ACK_STATUS`, in both relations. **Storage exclusivity makes that
  the whole of the response**: a responder inside a validated transfer plans no
  section at all, so the ACK carries none by construction rather than by a
  clip, and answering the admitted slot cannot become a way for a runtime
  section to cross during a transfer. Nothing is lost by the suppression: under
  the shared discipline each section is still due when the transfer closes and
  goes out on the next poll, and a suppressed section cannot retire without
  having crossed.
- **A section a relation cannot send MUST NOT be planned in that relation**,
  not merely clipped on the way out. Every latest-state section here is due
  until the wire confirms it, so clipping late leaves an ineligible one
  permanently due — and for the time anchor that means re-capturing a fresh
  sync-timer reading on every responder-snapshot publish, which makes the
  snapshot differ from the last one every time and turns the publish path into
  a storm — device-measured as one on a DUAL-HOST Right before the fix. The
  eligibility table is therefore read by the planner as well as by admission.
  RGB, the visual baseline and the storage news section each become ineligible
  in some relation and each has the same shape.
- The INPUT layer section is open **in DUAL-HOST only**, in both directions,
  on the shared discipline. It was that relation's whole runtime surface when
  the eligibility masks were written, and is not now: the relation carries six
  push sections and seven response sections (`split/era_split_wire_protocol.h`,
  `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH` / `..._RSP`).

  **That discipline is why the silence legs read under a constant poll
  cadence.** A poll carries a section only when one differs, so the section
  counter stays at zero across an idle window and across a typing window with no
  layer transition, while the frame counter rises at the poll rate.
- **The AUTHORITY section is open in both relations and both directions**, on
  the shared latest-state, edge-armed discipline.

  **Edge-consumed is not a style note here, it is the lane's one silent
  failure.** A level carried in every reply publishes to core0 at the poll
  rate, which returns the responder's `hkwork` to the poll rate — and no build,
  gate or `_Static_assert` sees it. Both ends therefore hold a shadow: the
  sender advertises only on a change, and the receiver reports to core0 only
  when the decoded record differs from the last one it delivered.

  **AUTHORITY never defers, and this is the deferral order's single
  statement.** The order is one rule in both relations, in the one shared
  planner: the one-byte facts (INPUT layer, lock, storage news, and the push
  direction's storage-pending bit) and AUTHORITY never defer — the
  never-deferring core — the yielding class defers to
  AUTHORITY and within itself by remaining budget, through room checks against
  the plan's projected length, and the anchor yields to everything. Within the
  yielding class the restart phase claims budget first, then ACTIVITY
  (judgment data with a live window behind it outranks a render refresh), then
  the visual baseline, then RGB state. **The arm leads the class because it is
  the one member that can establish a commit deadline**, and because its five
  bytes fit beside AUTHORITY at exactly fifteen. That is a capacity statement,
  not an apply-order promise: a matching PREPARED or COMMIT_ARMED answer may
  arrive on a later latest-state exchange. Both it and ACTIVITY sit in the yielding class
  rather than in the core because a one-poll deferral is noise against a 200 ms
  judgment window and against a commit window of hundreds of
  milliseconds. A deferred
  refresh stays due on its sent shadow and drains on the first poll AUTHORITY's
  edge-armed retirement leaves room, so an overrun is a one-poll deferral and
  not an encode failure.

  **A per-relation order — HOST-PEER keeps its earlier one, DUAL-HOST inverts —
  was weighed and rejected**: it preserves accepted behaviour bit-for-bit but
  adds a planner branch and keeps two orders forever, and the order it would
  preserve is a Slice 11.6 landing artifact (the newcomer section yielded so the
  accepted ones would not re-gate) rather than a judgement that a lighting
  refresh outranks the relation's only carrier of the responder's session facts.

  The order is held by a **family of `_Static_assert`s** in
  `era_split_wire_protocol.h`, not by one expression, stating two kinds of
  requirement, each against `ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN`.
  **Never-deferring-core** asserts say the sections the plan promises never to
  defer fit one compact frame in each relation and direction:
  `3 + INPUT_LAYER + AUTHORITY + STORAGE_NEWS` for DUAL-HOST,
  `3 + LOCK_STATE + AUTHORITY + STORAGE_NEWS` for HOST-PEER, and an
  authority-only frame — if that last fails, the relation has no carrier for
  its own revalidation. **Drain** asserts say each deferrable section (the
  restart arm, RGB state, ACTIVITY, the visual baseline, the anchor) fits
  beside the never-deferring one-byte facts, because a deferral that never fits
  never drains; ACTIVITY at eleven bytes fits beside *each* response-direction
  one-byte fact but not both at once, so both falling due on the poll an
  ACTIVITY drain is due defers that drain one further response poll. The
  restart phase carries a second drain assert of its own — that it fits
  beside AUTHORITY at exactly fifteen bytes — because an arm that could never
  share a frame with the section carrying the current answer would add a
  permanent capacity round trip to every agreement.
  `AUTHORITY + RGB` is 14 body bytes against 12, so those two never share a
  frame in either direction — an exclusion the family deliberately does not
  assert, because an exclusion assert would state the opposite of the
  requirement phrasing every other member uses.
- PEER runtime may accept `HOST_PEER_HOST_SOURCE_RSP` only as an alternate
  response to an in-flight HOST-PEER request.
- RGB-state runtime apply is no-EEPROM and covers only the low-rate state/config
  bytes plus sleep intent — in DUAL-HOST, configuration only, under the
  sleep-intent rule and the policy gate (`era_authority_contract.md`). RGB in
  HOST-PEER source-push, EEPROM, matrix, digest, mirror, per-frame RGB event
  stream, and any other concrete section bodies remain forbidden. The DUAL-HOST
  push RGB cell is an open exception, not an oversight in this line.
- As an inbound request to the HOST responder, it is explicitly rejected.

## Control-Only Payloads

- HEARTBEAT request and ACK_STATUS response are both one-byte compact control
  payloads, and neither takes a class/op id (the HOST-PEER class above states
  that MUST NOT once).
- Semantics come from route kind, relation mode, direction, and expected
  response context.
- `HOST_PEER_ACK_STATUS` is the no-section HOST response.
