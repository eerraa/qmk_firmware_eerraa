# ERA Wire Contract

Genre: contract
Canonical for: compact wire payload ids and packet shapes, closed ids, section marker tables and body layouts, section eligibility, SESSION_STATUS frame validity, the section disciplines and the deferral order

## Frame

Two lanes. Multi-byte fields are little-endian (`split/era_split_wire_protocol.h`).

| Lane | Header | Integrity | Payload | Frame |
| --- | --- | --- | --- | --- |
| compact | byte0 = `ERA_SPLIT_WIRE_FRAME_MARKER` `0xA0` \| direction `0x10` \| length `1..15` | CRC8 of marker plus payload (`payload_len + 1` bytes), last byte | `1..15` (`ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN` 15) | payload + 2; max `ERA_SPLIT_WIRE_COMPACT_MAX_FRAME_LEN` 17 |
| bulk page | byte0 = marker \| direction \| `ERA_SPLIT_WIRE_BULK_LENGTH_ESCAPE` 0; bytes 1..2 payload length | CRC32 of marker, length and payload (`payload_len + 3` bytes), last four bytes | `1..264` (`ERA_SPLIT_WIRE_BULK_PAGE_MAX_PAYLOAD_LEN` 264) | payload + 7; max `ERA_SPLIT_WIRE_BULK_PAGE_MAX_FRAME_LEN` 271 |

Direction bit set = `SECONDARY_TO_PRIMARY`. Compact length 0 is the bulk escape, not a compact frame. Bulk `frame_len` of 271 is also `ERA_SPLIT_COMMUNICATION_CORE_STORAGE_WIRE_FRAME_BYTES`.

## Control Byte

Payload byte0 on every lane (`split/era_split_wire_frame.c`):

| Bits | Field |
| --- | --- |
| 0..2 | `tx_seq` (`ERA_SPLIT_WIRE_CONTROL_TX_SEQ_MASK` 7). Sequence 1 through 7; zero is refused |
| 3..5 | `ack_seq` (`ERA_SPLIT_WIRE_CONTROL_ACK_SEQ_MASK` `0x38`) |
| 6 | `ERA_SPLIT_WIRE_CONTROL_EXT` `0x40` |
| 7 | `ERA_SPLIT_WIRE_CONTROL_RESERVED` `0x80`; any set bit refuses the frame |

`era_split_wire_next_seq()` in `split/era_split_wire_frame.c` walks 1..7.

## Compact Payload Kinds

`era_split_wire_payload_kind_t` in `split/era_split_wire_protocol.h`. `era_split_wire_classify_payload()` in `split/era_split_wire_payload.c` reports the kind. Class is `payload[1] & 0xF0` when EXT is set.

| Kind | Name | Packet |
| ---: | --- | --- |
| 1 | `GRANT_ACK` | compact, EXT clear, `payload_len == 1` |
| 2 | `SESSION_STATUS` | compact, EXT set, byte1 `0x10`, `payload_len == 9` |
| 3 | `EEPROM_SYNC` | class `0xE0`; compact fifteen-byte ops, or bulk-page `CHUNK_RSP` / `PUSH_CHUNK_REQ` |
| 4 | retired (`ERROR_NACK`, class `0x40`) | no encoder; `default` reject. Number stays allocated |
| 5 | `HOST_PEER` | class `ERA_SPLIT_WIRE_HOST_PEER_CLASS` `0x20`, op `0x20` |
| 6 | retired (`DUAL_HOST`, class `0x6x`) | no compiled id and no reservation; `default` reject. Number stays allocated |
| 7 | `HOST_PEER_HOST_SOURCE_RSP` | class `0x20`, op `0x21` |

`HOST_PEER_HEARTBEAT` and `HOST_PEER_ACK_STATUS` are route-context names over kind 1. They MUST NOT take class/op ids. Unmatched class/op, including `0x22` and `0x23..0x2F` of class `0x20`, is `default` reject.

## HOST-PEER Class

```text
ERA_SPLIT_WIRE_HOST_PEER_CLASS = 0x20

0x20: HOST_PEER_SOURCE_PUSH
0x21: HOST_PEER_HOST_SOURCE_RSP
0x22: unassigned
0x23..0x2F: reserved
```

Shared envelope, both ops (`split/era_split_wire_protocol.h`):

```text
byte0: control with EXT
byte1: op
byte2: 8-bit section mask
byte3..: bodies in ascending marker-bit order
```

Ops do not encode relation. The `HOST_PEER` envelope name is historical; renaming was declined as churn. Class `0x6x` carries no reservation. Markers, eligibility and closed cells: below.

## HOST-PEER Replacement Storage Class

Body layout of the seven domains, state machine, capacity, retry, and durable apply: `era_host_peer_storage_contract.md`. Status ids: `era_identifier_map.md`. Identical-image rule: `era_source_map.md`'s **Stored-Data Compatibility**. Removed V16 `ATTEST`/`BEGIN`/`DATA`/`COMMIT`/`ABORT` ops are not a compatibility surface (`era_closed_surface_contract.md`).

```text
ERA_SPLIT_EEPROM_SYNC_CLASS = 0xE0   (split/era_split_eeprom_sync.h)

0xE0: PROBE_REQ       compact request
0xE1: PROOF_RSP       compact response
0xE2: CHUNK_REQ       compact request
0xE3: CHUNK_RSP       bulk-page response
0xE4: APPLY_REQ       compact request
0xE5: APPLY_RSP       compact response
0xE6: COMPLETE_REQ    compact request
0xE7: CLOSE_RSP       compact response
0xE8: ABORT_REQ       compact request
0xE9: ABORT_RSP       compact response
0xEA: SYNC_STATUS_REQ compact request
0xEB: SYNC_STATUS_RSP compact response
0xEC: PUSH_CHUNK_REQ  bulk-page request
0xED: PUSH_RSP        compact response
0xEE: PUSH_CTL_REQ    compact request
0xEF: reserved
```

`era_split_eeprom_sync_response_operation()` in `split/era_split_eeprom_sync.h` pairs each request to one response. `PUSH_CHUNK_REQ` and `PUSH_CTL_REQ` both pair to `PUSH_RSP`. The sole alternate: an admitted `CHUNK_REQ` may also take `ABORT_RSP` with `SOURCE_CHANGED`. No other request accepts an alternate response operation.

Compact storage payload is always `ERA_SPLIT_COMMUNICATION_CORE_STORAGE_COMPACT_PAYLOAD_BYTES` 15 (`split/communication_core/era_split_communication_core_storage.h`), asserted equal to the compact max. Prefix, every compact and bulk storage op:

```text
byte0: control with EXT
byte1: op
byte2: domain
byte3: schema (`ERA_HOST_PEER_STORAGE_SCHEMA_V1` 1)
bytes 4..5: transaction generation, little-endian, must be nonzero
```

Remainder of the compact 15 (`split/communication_core/era_split_communication_core_storage.c`):

| Op | Bytes 6..14 |
| --- | --- |
| `PROBE_REQ` | 6..7 policy generation; 8..9 image size, nonzero; 10..13 image CRC32; 14 zero |
| `PROOF_RSP` | 6 status; 7..10 source revision (nonzero when status is `MATCH` or `TRANSFER`); 11..14 image CRC32 |
| `CHUNK_REQ` | 6..9 source revision, nonzero; 10 chunk id `< ERA_HOST_PEER_STORAGE_MAX_CHUNKS` 66; 11 length `1..ERA_HOST_PEER_STORAGE_CHUNK_BYTES` 252; 12..14 24-bit chunk-CRC hint (0 = no hint) |
| `APPLY_REQ`, `COMPLETE_REQ` | 6..9 source revision, nonzero; 10..13 image CRC32; 14 zero |
| `APPLY_RSP`, `CLOSE_RSP`, `PUSH_RSP` | 6 status; 7..10 source revision; 11..14 image CRC32. `PUSH_RSP` is this shape for every push request |
| `ABORT_REQ`, `ABORT_RSP` | 6..9 source revision; 10 status; 11..14 reserved zero |
| `SYNC_STATUS_REQ` / `RSP`, domain `ERA_SPLIT_EEPROM_SYNC_DOMAIN_NONE` 255 (`0xFF`) | 6 changed mask, bit7 clear; 7 baseline validity 0 or 1; 8..14 reserved zero. Summary RSP bytes 8..11 are the relation time-anchor seat: the validator REQUIRES them **permanently** zero. `TIME_ANCHOR` is the anchor's one carrier |
| `SYNC_STATUS_REQ` / `RSP`, real domain | 6 changed flag 0 or 1; 7 baseline validity 0 or 1; 8..9 16-bit divergence counter; 10..14 reserved zero |
| `PUSH_CTL_REQ` | 6 phase (`OPEN` 0 / `APPLY` 1 / `COMPLETE` 2 / `ABORT` 3); 7..10 source revision, nonzero. Abort: 11 abort-reason status, 12..14 zero. Else 11..14 full-image CRC32 |

Bulk-page ops `CHUNK_RSP` and `PUSH_CHUNK_REQ` only, one per direction. Shared 12-byte prefix then data; `payload_len == 12 + byte11`; payload at most 264; complete frame at most 271.

```text
bytes 0..5: prefix as above
bytes 6..9: source revision, nonzero
byte10: chunk id
byte11: data length
bytes 12..: data
```

`CHUNK_RSP` data length `0..252`. Length 0 is the content-match ACK for a `CHUNK_REQ` whose 24-bit hint was nonzero and matched the served chunk CRC. `PUSH_CHUNK_REQ` data length `1..252`, domain must be real; no zero-length form and no delta-hint mirror.

Bulk frames classify only in builds with `ERA_HOST_PEER_STORAGE_V1_ENABLE`. That bit on `SESSION_STATUS` is the same compile fact, not route authority (`split/era_split_qmk_rules.mk`).

`era_split_communication_core_storage_validate_wire_payload()` in `split/communication_core/era_split_communication_core_storage.c` admits compact `0xE0..0xE2`, `0xE4..0xEB`, `0xED`, `0xEE` and the two bulk forms; `era_split_wire_classify_payload()` in `split/era_split_wire_payload.c` reaches it.

## `SESSION_STATUS`

Carries HOST/no-HOST, `matrix_ready`, `bulk_page_supported` (only where
`ERA_HOST_PEER_STORAGE_V1_ENABLE` is compiled in), `usb_epoch`, host
open/close generations, and the response-request bit.

```text
0x01 accepted_host_open      0x10 status_response_requested
0x02 accepted_no_host        0x20 retired (was dual_host_ready)
0x04 retired (was storage changed hint)   0x40 matrix_ready
0x08 unassigned              0x80 bulk_page_supported
```

`era_split_wire_validate_session_status_payload()` in
`split/era_split_wire_payload.c` refuses the frame unless `payload_len == 9`
and byte1 is `0x10`, then:

| Check | Rule |
| --- | --- |
| reserved bits | refuse the **whole frame** for any bit outside `ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK`. `0x04`, `0x08` and `0x20` are reserved-and-rejected, not reserved-and-ignored |
| role | `flags & 0x03 ∈ {0x01, 0x02}`: exactly one role bit |
| `matrix_ready` | refuse `0x40` without `0x02`. When a no-host half may set it: `era_authority_contract.md`'s **Matrix Ready** |

Each carrier refuses whatever it has no fact for. A new bit is one line in
`ERA_SPLIT_WIRE_SESSION_STATUS_FLAG_MASK` and a whole-frame reject of older
halves. **Reassigning `0x20` stays banned** (was `dual_host_ready`); `0x08`
likewise dates its sender. Use:
`era_route_contract.md`'s **SESSION_STATUS Discovery And Liveness**.
Storage-changed rides `STORAGE_NEWS` (`0x40`).

## `HOST_PEER_SOURCE_PUSH`

Envelope: HOST-PEER Class, op `0x20`. Zero mask is invalid on this op
(section-less request is the one-byte compact control).

| Marker | Section | Bytes | Dir | Relation |
| --- | --- | ---: | --- | --- |
| `0x01` | MATRIX | `ERA_SPLIT_WIRE_HALF_MATRIX_BYTES` | push | HOST-PEER |
| `0x02` | INPUT_LAYER | 1 | push | DUAL-HOST |
| `0x04` | AUTHORITY | `ERA_SPLIT_WIRE_AUTHORITY_BYTES` 7 | push | both |
| `0x08` | RGB_STATE | `ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES` 7 | push | DUAL-HOST |
| `0x10` | ACTIVITY | `ERA_SPLIT_WIRE_ACTIVITY_BYTES` 11 | push | DUAL-HOST |
| `0x20` | VISUAL | 8 | push | DUAL-HOST |
| `0x40` | STORAGE_PENDING | 1 | push | both |
| `0x80` | RESTART_ARM | `ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ARM_BYTES` 5 | push | both |
| `0x01` | INPUT | 1 | rsp | DUAL-HOST |
| `0x02` | ACTIVITY | `ERA_SPLIT_WIRE_ACTIVITY_BYTES` 11 | rsp | DUAL-HOST (reused marker) |
| `0x04` | AUTHORITY | `ERA_SPLIT_WIRE_AUTHORITY_BYTES` 7 | rsp | both |
| `0x08` | lock-state | 1 | rsp | HOST-PEER |
| `0x10` | visual-resync | 8 | rsp | both (RGB-policy-gated) |
| `0x20` | RGB-state | `ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_RGB_STATE_BYTES` 7 | rsp | both |
| `0x40` | storage news | 1 | rsp | both |
| `0x80` | time anchor | `ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_TIME_ANCHOR_BYTES` 4 | rsp | both |

`ERA_SPLIT_WIRE_HALF_MATRIX_BYTES` is a board fact: seven on the nine-column
boards, nine on `sirind/tomak`. Visual width is reason plus that baseline
(eight / ten).

| Relation | `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_*_PUSH` | `..._RSP` |
| --- | --- | --- |
| HOST-PEER | MATRIX, AUTHORITY, STORAGE_PENDING, RESTART_ARM | AUTHORITY, LOCK, VISUAL, RGB, NEWS, ANCHOR |
| DUAL-HOST | INPUT, AUTHORITY, RGB, ACTIVITY, VISUAL, STORAGE_PENDING, RESTART_ARM (seven) | INPUT, ACTIVITY, AUTHORITY, VISUAL, NEWS, RGB, ANCHOR (seven) |

Storage-pending body:

```text
byte2 bit6: storage-pending marker
body byte0: bit0 unfinished pair work — settled divergence, decided cells, or a
            content-moving episode not yet closed
            (`ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_STORAGE_PENDING_FLAG_PENDING` `0x01`);
            bits 1..7 reserved zero, validator-refused
```

Composition and consumer: `era_host_peer_storage_contract.md`'s **Diagnostics**.
INPUT-class **forced first cross** (zero included). Initiator copy this way;
responder copy is `STORAGE_NEWS` bit7. **Summary and news do not stand in** —
they carry the settled phase only, so dirty stays invisible until
`ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS` 1000.

Restart arm body:

```text
byte2 bit7: restart-arm marker
body byte0: bits0..1 param (`ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_PARAM_MASK` `0x03`);
            bits2..3 act (`ERA_SPLIT_WIRE_HOST_PEER_SOURCE_PUSH_RESTART_ACT_MASK`);
            act 0 idle / 1 link / 2 CLEAN / 3 refused; bits4..7 reserved zero, refused
body byte1..4: T_commit, sync-timer ms, little-endian
```

Initiator carrier for both halves; responder request/PREPARED/COMMIT_ARMED ride
AUTHORITY. **Carries any act**; acts are `split/era_split_restart_agreement.h`'s.

| Act | wire param | `T_commit` | Meaning |
| --- | ---: | ---: | --- |
| idle | 0 | 0 | canonical all-zero body |
| link speed | the validated link-level parameter | nonzero | the existing shared-deadline arm |
| EEPROM CLEAN | 1 | 0 | `PREPARE`; no deadline exists |
| EEPROM CLEAN | 2 | nonzero | `COMMIT`; the shared deadline |

CLEAN param 0 or 3 is malformed. Act 3 is refused in every form. `param_max`
stays 0; values 1 and 2 never reach local CLEAN dispatch as an application
argument.

`PREPARE` (zero timestamp, not a deadline) then `COMMIT` only after both local
and peer PREPARED; a PREPARED echo is not COMMIT_ARMED. Why:
`era_host_peer_storage_contract.md`'s **Why An EEPROM Clean Is An Agreed Restart**.

**A nonzero deadline is absolute, not a countdown.** PREPARE is the one admitted
zero-deadline live form (CLEAN/param-1). The section forces one cross per
relation: idle as all-zero, a live arm as the same absolute deadline. Rotation
retires an unconfirmed CLEAN COMMIT; a previous relation's PREPARED/COMMIT_ARMED
is never current. Local quarantine and the prepare obligation survive identity
change until the controlled reset in `era_host_peer_storage_contract.md`. Fresh
boot publishes only idle.

Yielding class's first claimant (`3+1+7+1+5` / `3+7+1+5` miss the budget). Fits
beside AUTHORITY at `ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN` 15; not an
apply-order promise.

`VISUAL` is the pressed-baseline twin at the response width. Hit tracker only;
no matrix, no action (`era_invariants.md`'s render-state clause). Reason is
core1's (`RELATION_OPEN` once per relation/reopen, then `RENDER_RESET`).
`RGB_STATE` is the response body byte for byte; DUAL-HOST sleep bit is zero at
capture and skipped at apply.

**`MATRIX` and `AUTHORITY` never share a HOST-PEER frame** (`3+7+7` over budget).
Matrix keeps `HOST_PEER_MATRIX_SOURCE_PUSH`; authority rides the standing
exchange (`era_route_contract.md`). A dual mask is a malformed frame.

| Rule | Contract |
| --- | --- |
| mask walk | `byte2` is a section mask. The validator walks it, sums declared body lengths, and MUST land exactly on `payload_len`. An unassigned marker bit is a reserved-bit rejection |
| zero mask | invalid on push (section-less request is the one-byte compact control). On response, `byte2 == 0x00` is the no-section envelope and stays valid |
| eligibility | which sections a relation may send and accept is one linked `const` table at one send site and one admission site per direction. The gates read the table's bytes (`era_performance_gates.md`) |
| `STORAGE_PENDING` | eligible in both relations' push cells (initiator = HOST-PEER PEER or DUAL-HOST Left). Responder's copy rides `STORAGE_NEWS` bit7. Joins the never-deferring one-byte facts |

> **REFUSED:** restore the layout walk's variable-length machinery.
> **WHY:** its bound is the `payload_len != 3 + fixed_total` arm, which
> the parameter selected around rather than provided — the machinery
> never carried the check it appeared to.
> **REOPENS:** a section whose body length is genuinely variable.

| Closed cell | Why | Assert |
| --- | --- | --- |
| full row-array matrix | forbidden | — |
| digest-only matrix response | forbidden | — |
| EEPROM DATA | forbidden | — |
| RGB in HOST-PEER push | PEER is dark and renders the HOST's config; the response direction is that relation's only RGB carrier | `era_split_wire_protocol.h` beside the eligibility entry |
| INPUT in HOST-PEER | PEER never resolves keycodes; HOST composed rows already carry a PEER-held layer key | same |
| ACTIVITY in both HOST-PEER cells | that relation is one pipeline; the HOST tapping engine already sees every key of both halves as a local event | stays-closed asserts, both cells |

## `HOST_PEER_HOST_SOURCE_RSP`

Envelope: HOST-PEER Class, op `0x21`. `byte2 == 0x00` is a valid no-section
envelope.

Markers, widths and per-relation eligibility are the master table above.

**Every section in both id spaces is latest-state and edge-armed** — advertised
while live value differs from the last confirmed, retired on confirm, no timer.
Shadows drop on identity rotation; a sent shadow advances from the wire's
section byte, so a section cannot retire without having crossed.

**Lock-state (`0x08`) is HOST-PEER-only by structure**: DUAL-HOST halves each
receive their own host LED report. A DUAL-HOST lock-desync capture is the
falsifier. **A new marker takes a value never assigned in either direction**
(`sec=` never ambiguous; same rule as the `0x20` ban). ACTIVITY `0x02` reuse
is the recorded exception.

**Visual-resync + RGB-state is invalid in either direction** (`8+7` body vs 12
left): no legal `payload_len`; send-clip drops RGB when visual is present.

Lock-state body:

```text
byte2 bit3: lock-state marker
body byte0: bits0..2 Num/Caps/Scroll (`ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_LOCK_STATE_VALUE_MASK` `0x07`); bits 3..7 reserved zero
```

INPUT layer body:

```text
byte2 bit0: INPUT layer section marker
body byte0: the sending half's layer_state
```

AUTHORITY body, identical in both directions:

```text
byte2 bit2: AUTHORITY marker (both directions)
body byte0: bit0 host_open, bit1 no_host, bit2 matrix_ready,
            bits3..4 param (`0x18`), bits5..6 act (`0x60`; 0 idle, 1 link, 2 CLEAN, 3 refused),
            bit7 qualified (`0x80`; armed / PREPARED / COMMIT_ARMED)
body byte1..2 usb_epoch, 3..4 host_open_gen, 5..6 host_close_gen (LE)
```

Session facts minus `bulk_page_supported`, plus restart intent.
`bulk_page_supported` cannot change inside a session (`era_authority_contract.md`);
**a consumer MUST leave that peer-cache field untouched**. Shared validation:
exactly one role bit, `matrix_ready` only on no-host. Mask `0xFF` asserted
(`era_split_wire_protocol.h`). **The next fact needs its own home, not a hidden
bit.**

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

Responder agreement rides this body because the response mask is full
(`era_closed_surface_contract.md`). Five restart bits are one act-qualified
state, not independent flags.

| Act | wire param | bit7 | Meaning |
| --- | ---: | ---: | --- |
| idle | 0 | 0 | no restart state |
| link speed | validated link-level parameter | 0 | request |
| link speed | the same parameter | 1 | matching shared deadline adopted |
| EEPROM CLEAN | 0 | 0 | `REQUEST` |
| EEPROM CLEAN | 1 | 1 | local physical boot-replay proof complete: `PREPARED` |
| EEPROM CLEAN | 2 | 1 | CLEAN COMMIT deadline adopted: `COMMIT_ARMED` |

CLEAN `(param 1, bit7 0)`, `(param 2, bit7 0)`, `(param 0, bit7 1)` and every
param-3 form are malformed. The full tuple is in the sender shadow and receiver
edge; relation identity is part of acceptance. **REQUEST before either qualified
state.** COMMIT_ARMED is the only arm confirmation.

`era_split_wire_authority_equal()` in `split/era_split_wire_payload.c` is the
sender shadow and receiver edge (intent must cross).
`era_split_scheduler_session_note_peer_authority()` in
`split/era_split_scheduler_session.c` ignores restart fields (not a session
edge). The 16-bit values are change detection only. This section is why
`SESSION_STATUS` can stop post-relation (`era_route_contract.md`).

**INPUT is one byte** (`layer_state_t` 8-bit, asserted in
`split/era_split_peer_layer.c`). Every value is valid.

ACTIVITY body, identical in both id spaces (marker `0x02` here and `0x10` in
the push direction):

```text
byte2 bit1: ACTIVITY marker
body byte0: bit0 window open, bits 1..7 reserved zero
body byte1 press counter mod 256; byte2 release counter mod 256
body byte3..6 last-press ms LE; byte7..10 last-release ms LE
```

Window flag is derived from `era_tapping`'s bridge (permissive hold, hold on
other key press, retro tapping; all default off). Fields live only while the
**peer's** window is up. **Timestamps are the judgment; counters dedup.**
Press+release predominantly cross in one image, so a later-image-only rule
starves. Invalid sent shadow compares against all-zero, not a forced send.
**Marker `0x02` is reused** (owner decision); capture-era rule:
`era_capture_reading.md` `sec=`.

Owner decisions: DUAL-HOST no-added-hop is the product; skew is physical;
**arming is derived, not owned.**

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

Approximations: one stamp per edge (newest wins); a pair whose LT release beats
one-housekeeping delivery still taps.

Visual-resync body:

```text
byte2 bit4: visual-resync marker
byte3 reason: 0 RELATION_OPEN, 1 TX_OVERFLOW (no sender), 2 TICK_GAP,
              3 RELATION_REOPEN (no sender), 4 RENDER_RESET = `ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_VISUAL_RESYNC_REASON_MAX`
byte4..: packed baseline, N = ERA_SPLIT_WIRE_HALF_MATRIX_BYTES,
         bit = row * MATRIX_COLS + col; unmapped/high bits zero
```

**The body is fixed at reason plus baseline in both directions, and there is no
reason-only form.** A capture whose visual section is one body byte is a
pre-2026-08-10 image.

Reasons `1` and `3` have no sender; replay names only `RELATION_OPEN` and
`TICK_GAP`. Those two values stay accepted (plain diff). **Validator is
deliberately not narrowed** to the three producible reasons.

RGB-state body (identical in the push id space at marker `0x08`):

```text
byte2 bit5: RGB-state marker
body byte0: bit0 enabled, bit1 sleep, bits2..7 zero
body byte1 mode (bits6..7 zero); byte2 hue; byte3 sat; byte4 val; byte5 speed; byte6 LED flags
```

**The sleep bit does not cross in DUAL-HOST.** Apply publishes the bit and
does not write the render gate (`era_authority_contract.md`'s **Lighting Sleep
Ownership**). Captured bit = HOST's resolved sleep decision. Both-halves-dark
belongs in the sleep decision, never a wire apply.

Storage news body:

```text
byte2 bit6: storage-news marker
body byte0: bits0..6 value (`ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_VALUE_MASK` 127, hex `0x7F`);
            1 to 127 forward-only, 0 = nothing; bit7 pending (`ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_STORAGE_NEWS_FLAG_PENDING` `0x80`)
```

Constant `ERA_SPLIT_WIRE_HOST_PEER_HOST_SOURCE_RSP_SECTION_STORAGE_NEWS`;
carriers `host_source_storage_news[_valid]`, `peer_storage_news[_valid]`,
`send_storage_news`. Bit7 set dates the image at or after 2026-08-14; no
reserved bits left. `SETTLED_DIRTY_MASK` survives only in
`communication_core/era_split_communication_core_standing.h`. Meaning:
`era_host_peer_storage_contract.md`'s **Storage News Value And Relation-Open Audit**.

Relation time-anchor body:

```text
byte2 bit7: time-anchor marker
body byte0..3: responder sync-timer ms at snapshot publish, little-endian
```

Encode order is ascending marker bit; priority differs for ACTIVITY. Valid
`payload_len` is 3 plus declared bodies. RGB+visual has no legal length.
Anchor fits `3+8+4` on seven-byte half-matrix boards.

- Slot-only: HOST response to an admitted HOST-PEER PEER heartbeat or
  source-push with matching ACK. HOST independent send is forbidden.
- Time authority is the responder in both relations. **Both ends corrected**
  (`ahold`, `era_capture_reading.md`'s **The shared clock and the time anchor**).
  Once per open/reopen, then a slow bounded cadence. Relation-scoped: no new
  shared-clock restart deadline until a TIME_ANCHOR from that relation applies.
- News **asks, names nothing**. Zero is legal. Forced nonzero refresh on
  `ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS` 1000; gate `value != 0` is boot-scoped.
  **Forced cross per open, zero included** (post-rotation `0x00` never crossed
  → mirror stuck lit). Repeat stop is an **open question**. One carrier:
  `era_route_contract.md`'s **One carrier for the response section set**.
- Source-push answer is the bare ACK (no response section). Under storage
  exclusivity the ACK is the whole response. **MUST NOT plan ineligible
  sections** — late clip leaves them permanently due; time-anchor re-capture
  storm was device-measured on a DUAL-HOST Right.
- INPUT is **DUAL-HOST only** (seven push and seven response:
  `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_PUSH`,
  `ERA_SPLIT_WIRE_SECTION_ELIGIBLE_DUAL_HOST_RSP`). Section counter stays zero
  across idle/no-layer-change; frame counter rises at the poll rate.
- AUTHORITY is open in both relations and both directions. **Edge-consumed**:
  a level in every reply returns `hkwork` to the poll rate; both ends hold a
  shadow. **AUTHORITY never defers** — this is the deferral order's single
  statement.

| Class | Members | Rank |
| --- | --- | --- |
| never-deferring core | INPUT, lock, storage news, push STORAGE_PENDING, AUTHORITY | never defers |
| yielding | RESTART_ARM, ACTIVITY, visual, RGB | RESTART_ARM > ACTIVITY > visual > RGB |
| anchor | TIME_ANCHOR | yields to everything |

Arm leads because it can set a commit deadline and fits beside AUTHORITY at
fifteen. A one-poll deferral is noise against a 200 ms judgment window.

> **REFUSED:** a per-relation deferral order — HOST-PEER keeps its earlier one, DUAL-HOST inverts.
> **WHY:** it preserves accepted behaviour bit-for-bit but adds a planner branch and keeps two orders forever, and the order it would preserve is a landing artifact rather than a judgement that a lighting refresh outranks AUTHORITY.
> **REOPENS:** a measurement that the two relations need different yielding ranks.

`_Static_assert` family in `era_split_wire_protocol.h` against
`ERA_SPLIT_WIRE_COMPACT_MAX_PAYLOAD_LEN` 15:

| Kind | Requirement |
| --- | --- |
| never-deferring DUAL-HOST | `3 + INPUT + AUTHORITY + STORAGE_NEWS` (rsp) and `3 + INPUT + AUTHORITY + STORAGE_PENDING` (push) |
| never-deferring HOST-PEER | `3 + LOCK + AUTHORITY + STORAGE_NEWS` (rsp) and `3 + AUTHORITY + STORAGE_PENDING` (push) |
| authority-only | `3 + AUTHORITY`; if this fails the relation has no carrier for its own revalidation |
| drain | each deferrable (arm, RGB, ACTIVITY, visual, anchor) fits beside the never-deferring one-byte facts |
| ACTIVITY drain | eleven bytes fit beside *each* one-byte fact but not both at once |
| arm beside AUTHORITY | `3 + 7 + 5` lands on fifteen |
| AUTHORITY + RGB | 14 body bytes against 12; exclusion deliberately not asserted |

- PEER runtime may accept `HOST_PEER_HOST_SOURCE_RSP` only as an alternate
  response to an in-flight HOST-PEER request.
- RGB apply is no-EEPROM, config plus sleep (DUAL-HOST: configuration only;
  `era_authority_contract.md`). HOST-PEER push RGB, EEPROM, matrix, digest,
  mirror, per-frame RGB remain forbidden. DUAL-HOST push RGB is an open
  exception.
- As an inbound request to the HOST responder, it is explicitly rejected.

## Control-Only Payloads

Kind 1 (`GRANT_ACK`): compact, EXT clear, `payload_len == 1`.
`HOST_PEER_HEARTBEAT` (request) and `HOST_PEER_ACK_STATUS` (response) are that
packet. Neither takes a class/op id. Semantics come from route kind, relation
mode, direction, and expected response context — not from a second wire id.
