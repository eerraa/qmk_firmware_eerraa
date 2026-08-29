# ERA Replacement Storage Contract

Status: active
Genre: contract
Canonical for: ERA logical EEPROM ownership, ERA NVM durability, the seven-domain
cross-half storage protocol, replacement Apply, dynamic-macro durability, State
Sync revisions, EEPROM CLEAN, storage arbitration and recovery
Read when: changing any persistent ERA/VIA/QMK setting, the custom EEPROM
adapter, ERA NVM, storage wire operations, State Sync, or EEPROM CLEAN

This contract describes the current production architecture only. Git carries
the retired implementation history; an active reader must not have to choose
between two persistence models.

## Scope And Authority

ERA RP2040 storage is one stack:

```text
VIA / eeconfig / dynamic keymap / normal QMK features
                    |
            stock QMK EEPROM API
                    |
             EEPROM_DRIVER=custom
====================|==================== QMK / ERA boundary
                    |
          era_eeprom_driver.c
                    |
               ERA NVM v1
                    |
          RP2040 NOR backend
```

QMK owns the public EEPROM API and the logical layout conventions its ordinary
callers use. ERA owns persistence below that API. QMK Core does not know about
ERA NVM banks, journal records, flash verification, storage dirty domains,
split replacement transactions, or CLEAN replay proof.

The QMK-visible logical EEPROM is exactly 24 KiB (`24576` bytes). Production ERA
RP2040 builds that adopt this storage bundle select `EEPROM_DRIVER=custom` and
`EEPROM_SIZE=24576`. QMK wear-leveling is not linked into an ERA production
storage build.

`keyboards/era/common/storage/era_eeprom_driver.c` is the adapter. It owns:

- ERA NVM mount/init;
- stock QMK logical reads from ERA NVM's 24-KiB public RAM image;
- stock QMK synchronous writes through the result-bearing NVM engine;
- whole-store format/erase through `era_nvm_format()`;
- successful local committed-span notification;
- result-bearing remote replacement and CLEAN prepare helpers;
- one-sector inactive-bank background maintenance.

An ordinary QMK EEPROM write has no error return. ERA therefore keeps the
result-bearing API below that void surface. Replacement Apply and CLEAN never
derive durable success from a void QMK call.

## ERA NVM Physical Contract

The effective build-visible RP2040 flash remains 2 MiB. The final 128 KiB is
reserved for ERA NVM and is excluded from the firmware load image by
`ERA_RP2040_SRAM_RESIDENT.ld`:

```text
ERA NVM region: 128 KiB
Bank A:          64 KiB
Bank B:          64 KiB
```

Each bank contains immutable metadata, a complete 24-KiB snapshot, a commit-last
activation record, and an append-only variable-length journal. Physical program
pages are 256 bytes and erase sectors are 4 KiB. Correctness requires only those
common NOR primitives; no 64-KiB block erase is required.

At mount, the production parser accepts only complete, CRC-valid,
format-compatible banks and committed journal records. The newest valid bank is
authority. A newer incomplete bank never displaces an older valid bank.

The external power-loss guarantee for one ERA NVM transaction is **old or new,
never a partially public range**:

- before the record/bank commit authority, mount recovers the old image;
- after the commit authority, mount recovers the new image;
- the public RAM range changes only after durable commit succeeds.

An append failure seals that tail. A later durable write rotates rather than
guessing whether an ambiguous slot can be reused. Bank construction activates
last. Program and erase operations are read back immediately; a verification
failure is a failed NVM result, never a successful cache equality.

The physical format version (`ERA_NVM_FORMAT_VERSION`) is distinct from the
logical ERA EEPROM reset key (`ERA_EEPROM_RESET_KEY`). A physical format change
and a logical owner-map change are separate compatibility decisions.

## Current Storage Inventory

The seven portable domains synchronize semantic logical ranges, never physical
NVM metadata.

Schema 1 is geometry-parameterised for the dynamic keymap: four layers ×
`MATRIX_ROWS` × `MATRIX_COLS` × two bytes. The macro base follows that range.
Every other domain address/size is geometry-independent. The two halves of a
pair run one identical firmware image, and probe/proof identity rejects schema
or size mismatch before content moves.

| Domain | Id | Logical owner/source range | Schema | Bytes | Target reload |
| --- | ---: | --- | ---: | ---: | --- |
| `ERA_CONFIG` | 0 | logical `37..212` | 1 | 176 | ERA common + board reload |
| `DYNAMIC_KEYMAP` | 1 | base 297, 4 × rows × cols × 2 | 1 | geometry | none; QMK reads EEPROM image |
| `DYNAMIC_MACRO` | 2 | immediately after keymap | 1 | 16384 | none; QMK reads EEPROM image |
| `QMK_RGB_MATRIX` | 3 | logical offset 23 | 1 | 8 | RGB Matrix reload |
| `QMK_KEYMAP_CONFIG` | 4 | logical offset 4 | 1 | 2 | `keymap_config` reload |
| `QMK_DEFAULT_LAYER` | 5 | logical offset 3 | 1 | 1 | default-layer apply |
| `VIA_LAYOUT_OPTIONS` | 6 | logical offset 296 | 1 | 1 | VIA layout-options apply |

On a 12×9 board the keymap is 864 bytes at `297..1160` and macro is
`1161..17544`. On the 12×11 `sirind/tomak` the keymap is 1056 bytes and macro
starts at `1353`.

The ERA config block remains 256 bytes at logical offset 37. Its first 176 bytes
are portable `ERA_CONFIG`; bytes `176..255` are local/protected and never cross
the storage wire. The interior owner map is canonical in
`storage/era_eeprom_layout.h`. Rearranging that local map requires the existing
`ERA_EEPROM_RESET_KEY` policy even when wire schema 1 remains the same.

## Excluded Local State

The following are never portable storage payload:

- local sync-policy flags and divergence counters;
- split link level;
- reset guard / EEPROM reset key record;
- per-domain convergence baseline record;
- protected reserve bytes;
- ERA NVM physical metadata, generations and journal structure;
- runtime transaction generations, route state and diagnostics.

The wire synchronizes owner content. It never clones physical storage state or
local arbitration metadata.

## Normal Local Writes And Dirty Publication

The authoritative local-write notification is below QMK Core. After a durable
`LOCAL_QMK` commit succeeds, the custom adapter reports the exact changed
logical span to ERA. No QMK `nvm_*` weak hook is part of this boundary.

Dynamic-macro staging is the deliberate display-only exception to the phrase
"after a durable commit" above. The ERA NVM transaction mode becomes open on
the nonzero macro marker and stays open through payload staging and the durable
close attempt. The storage indicator may read that O(1) RAM fact immediately so
an operator sees unfinished pair work from the first macro write. **Opening or
staging a macro is not a committed-span notification**: it advances no State
Sync revision, starts no settled-dirty recency claim, and proves no durable
content. The ordinary full-domain committed-span notification still happens
only after the final zero has committed successfully.

The notification has two consumers:

1. State Sync classifies the span into KEYMAP, MACRO or CONFIG semantic
   revisions;
2. split storage marks every overlapping portable domain dirty and starts that
   domain's trailing quiet deadline.

The trailing quiet interval is 1000 ms. Repeated writes to one domain replace
its deadline and dirty generation. At settle, one whole-domain capture becomes
the immutable source image for storage arbitration. There is no periodic idle
patrol.

ERA's accepted 500-ms UI persistence coalescing remains separate from this
1000-ms storage settle. VIA RGB Matrix/RGBLight and ERA keyboard-channel sliders
may defer their approved EEPROM save so a drag does not create one flash record
per step. A controlled reset or suspend flushes an approved pending save before
the live lighting object is changed or discarded. This is an accepted
user-visible durability/endurance policy and is not a second persistence engine.

## Dynamic Macro Transaction

The externally observable upload transcript is fixed:

```text
RESET -> FF opener -> payload -> zero close -> targeted marker read
```

The marker is the final byte of the 16-KiB macro domain. Nonzero means the
candidate is invalid/open; zero means the close has committed durably.

For a normal upload:

1. a nonzero final-byte write opens staging and therefore the display-only
   storage-pending arm immediately; the existing STORAGE_PENDING/STORAGE_NEWS
   carrier mirrors that unfinished-work fact to the peer;
2. payload writes update the single public 24-KiB RAM image but do not each
   create a durable record or semantic publication;
3. the public marker remains nonzero, so QMK cannot execute the staged image;
4. the final zero performs one whole 16-KiB ERA NVM transaction;
5. only after that durable transaction succeeds is marker zero public and the
   macro committed-span notification emitted; that notification takes over the
   ordinary dirty/settle arm before the NVM transaction mode returns to IDLE,
   so the indicator has no close-boundary gap;
6. a failed close restores/retains a nonzero marker and advances no MACRO State
   Sync revision; the open transaction continues to report unfinished storage
   rather than falsely going dark.

The macro transaction gates **only durable writes whose ranges touch the macro
domain**. A keyboard-originated EEPROM write outside that range — including the
deferred RGB Matrix flush that can occur during an upload — remains durable and
survives the next mount. A result-bearing replacement touching the macro domain
while staging is open is `BUSY`/refused. This asymmetry is the rule.

### Stock QMK macro RESET transcript is a bound dependency

The current implementation recognizes `nvm_dynamic_keymap_macro_reset()`
(`quantum/nvm/eeprom/nvm_dynamic_keymap.c`) without a QMK Core hook. Stock QMK
scans the macro domain sequentially through `eeprom_update_block()`
(`drivers/eeprom/eeprom_driver.c`) with a local `uint8_t dummy[16]`; the update
helper reads each 16-byte block before deciding whether a zero write is needed.
`era_nvm_qmk_read()` observes that exact sequential 16-byte read transcript and
`era_nvm_qmk_write()` stages the corresponding zero writes as one RESET
transaction.

This is a cross-file source contract: `ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES` is
16 and `quantum/nvm/eeprom/nvm_dynamic_keymap.c` must retain the sequential
16-byte stock reset loop unless this recognition rule is deliberately replaced.
`tests/era_nvm_qmk_driver` compiles that **stock QMK source file itself** and
calls `nvm_dynamic_keymap_macro_reset()`, so a transcript change fails the test
instead of silently disabling RESET.

An unrelated EEPROM read interleaved into that scan aborts recognition. The
current stock loop has no such re-entry point. A future owner may instead choose
the separately reviewed aligned-all-zero 16-byte recognition rule, but that is
not the production rule today and must not be introduced implicitly.

## External H7S State-Sync And Macro Handoff

H7S/VIA compatibility uses the existing 32-byte VIA RAW HID surface. No new
command id, report, transaction id, capability bit or BUSY state is added.

State Sync is `GET_KEYBOARD_VALUE` (`0x02`), selector `0x06`, envelope version
`0x01`:

- request byte 3 and bytes `6..31` are zero;
- bytes `4..5` are an opaque echoed tag;
- OK status is `0x00` in byte 3;
- byte 6 is domain mask `0x07`, byte 7 zero;
- bytes 8, 12 and 16 hold nonzero big-endian 32-bit KEYMAP, MACRO and CONFIG
  revisions;
- bytes `20..31` remain zero.

KEYMAP means the dynamic keymap/encoder image changed. MACRO advances only on a
durably completed macro upload or standalone macro RESET. CONFIG means a
GET-visible configuration value changed, whether at semantic SET-before-SAVE,
an independent durable local write, or successful remote-domain publication.
These are nonzero wrapping RAM generations, not durable NVM epochs.

The H7S side consumes the public transcript and revisions. It does not copy ERA
NVM banks, split authority, replacement Apply internals or RP2040 flash policy.

## Recency Layer

Each portable domain has a persisted convergence baseline CRC and a local
16-bit divergence counter. The baseline record is protected local EEPROM at
config offsets `220..251`; the counters remain in the local policy block. They
never cross as portable domain bytes.

A settled local capture compares its CRC with the baseline:

- equal: changed=false and divergence counter becomes zero;
- different: changed=true and the counter increments, saturating at
  `UINT16_MAX`;
- invalid baseline record: arbitration degrades conservatively to changed.

At a confirmed convergence close, both sides write the agreed CRC as the new
baseline and clear that domain's divergence counter. A boot with a valid
baseline repairs only obviously stale counter state; it does not count the boot
itself as an edit.

## Arbitration

Every direction decision is derived from a whole-family `SYNC_STATUS` view of
both halves' current changed masks. For each domain:

| local changed | peer changed | action |
| --- | --- | --- |
| no | no | relation-open verification only |
| no | yes | probe/pull |
| yes | no | push |
| yes | yes | conflict counter exchange |

For a conflict, the larger divergence counter wins; a tie resolves to Left.
Direction is valid only for the arbitration round that produced it. Any abort
that requires retry returns through a fresh summary rather than remembering an
old direction.

Every serviced relation starts with a mandatory verify-all audit. An in-session
storage-news edge arms a summary but does not force unchanged domains through
new MATCH episodes.

## Storage News Value And Relation-Open Audit

The responder's storage hint is a forward-only nonzero 7-bit news value, not a
domain claim. Each settled divergent capture advances it; zero means no claim.
A new nonzero value tells the peer only to request a fresh summary. The summary,
not the hint, decides domains and direction.

The advertised storage-pending bit is separate from the news value and carries
the user-visible unfinished-work fact. The local indicator is this half's
visible work union the peer's advertised pending mirror, gated by serviceability
and local EEPROM sync policy. Boot-only conservative baseline uncertainty stays
display-provisional until an actual transfer/fault proves pair work.

## Relation Admission

The same storage engine serves HOST-PEER and DUAL-HOST. Relation mode changes
only who is admitted as initiator/responder; it does not select a second storage
implementation.

Admission requires the existing settled relation identity, Core1 owner/role,
bulk-page capability and EEPROM policy rules. The responder may remain admitted
to answer a policy-closed refusal; the initiator requires its local EEPROM sync
policy requested. Wire section eligibility remains canonical in
`era_wire_contract.md` and `era_route_contract.md`.

## Source Revision And Identity

A source revision is a nonzero 32-bit immutable-capture generation. CRC32 is
content/integrity identity. They are not interchangeable.

An active storage handoff is fenced by every relevant generation: owner epoch,
relation generation, request/snapshot generation, policy generation, storage
transaction generation, domain, schema, chunk id where applicable, source
revision, image size and CRC.

Generation zero is invalid. A 16-bit generation is not reused inside one
relation; exhaustion rotates relation identity before returning to one. A
source-revision wrap likewise rotates and invalidates cached manifests before
revision one can be published again.

## Fixed Wire Contract

The storage wire envelope remains the existing compact `0x20`/`0x21` family.
Probe/proof, chunk, apply, complete/close, abort, push-control and sync-status
operations keep their existing operation ids, payload direction, CRC/generation
authority and Core1 ownership. The Session 2 persistence cutover changes no H7S
or VIA envelope.

Core1 owns physical wire execution and copies only from immutable published
storage images/snapshots. Core0 owns QMK state, domain capture, arbitration,
admission, NVM Apply, runtime reload, manifests and semantic publication.

## Transaction State Machine

### Pull

```text
summary / probe
  -> immutable source proof
  -> chunk transfer into candidate staging
  -> full candidate CRC/schema validation
  -> APPLY_READY exchange
  -> revalidate every fallible local authority/precondition
  -> ADMIT
  -> synchronous era_nvm_replace(domain, candidate, REMOTE_APPLY)
  -> durable success
  -> runtime reload
  -> immutable manifest + State Sync publication
  -> relation/status revalidation as required
  -> COMPLETE / convergence baseline
```

### Push Transaction

The initiator publishes its immutable source and opens a push. The responder
stages the acknowledged chunk stream, validates the whole candidate, receives
the apply trigger, revalidates every fallible local precondition, then crosses
the same ADMIT boundary and calls the same synchronous `era_nvm_replace()`.
After durable success it reloads runtime state, publishes manifest/State Sync,
declares durable, answers COMPLETE and performs the existing relation close /
identity rotation rule.

Both directions therefore share one persistence boundary. Direction changes
wire roles, never NVM semantics.

## Replacement Apply: ADMIT And Public Authority

ADMIT is the final cancellation boundary before persistence. Before it, Apply
must revalidate at least:

- domain/schema/image size;
- full candidate CRC;
- ERA_CONFIG reserved-zero integrity;
- current relation/owner/role/policy identity;
- peer/source generations applicable to the direction;
- target not dirtied since transfer started;
- manifest/source revision capacity;
- ERA NVM ready state;
- macro-domain availability.

If any precondition fails, no candidate NVM transaction starts.

After ADMIT, relation/policy/source change is **not rollback authority**. The
synchronous NVM call finishes. Its own atomicity decides old or new. When the
call returns, the storage episode handles whatever current relation exists from
the durable state that won.

`era_nvm_replace()` publishes the changed public RAM range only after its durable
commit authority succeeds. Ordinary QMK readers therefore see the complete old
domain throughout an in-progress replace and the complete candidate immediately
after durable success. There is no generic alternate EEPROM view.

An NVM failure leaves public RAM old and the episode visibly fails. No success
manifest, State Sync revision or COMPLETE follows.

Once NVM succeeds, NVM is canonical. A later runtime/manifest/Core1 publication
failure repairs forward: core0 rereads the committed domain from the public NVM
image, rebuilds its immutable publication, and re-arbitrates/re-proves if the
relation no longer matches. It never rewrites EEPROM back to the old candidate
solely to repair transport/publication.

Remote Apply uses NVM origin `REMOTE_APPLY`; the custom adapter therefore does
not report it as a fresh local dirty edit. The Apply completion path explicitly
owns manifest, convergence baseline and State Sync publication.

## Retry, Duplicate, And Failure Semantics

Wire retry/duplicate handling is unchanged: request and response identities are
generation-matched, duplicate responses are idempotent, and timeouts/stale
results cannot be applied under a new relation.

Storage failure classes are kept separate:

- transfer/protocol/identity failure: no ADMIT, no NVM change;
- NVM program/erase/verification failure: old public image remains unless the
  commit authority had already made the new image canonical; replay decides;
- post-NVM runtime/publication failure: canonical NVM remains new and recovery
  goes forward from it;
- terminal policy/schema/domain refusal: no repeated direction loop without a
  new event;
- macro-domain busy: remote replacement touching an open macro is refused.

The engine exposes physical program/erase counts and failure counts. Healthy
device acceptance requires `program_failure_count == 0` and
`erase_failure_count == 0`.

## Inactive-Bank Maintenance And Rotation

After mount/rotation, the inactive bank is erased opportunistically by
`era_eeprom_driver_maintenance_task()` (`storage/era_eeprom_driver.c`).
`era_common_features_task()` (`system/era_common_features.c`) calls it at
top-level keyboard cadence and each call erases at most one 4-KiB sector.

The NVM layer never calls keyboard, matrix, wire or scheduler work from inside a
program/erase primitive. This is deliberately non-recursive. Returning to the
caller between sectors gives normal keyboard/wire work an opportunity without
making the NVM engine aware of it.

If background maintenance has not finished when rotation is mandatory, the
rotation synchronously completes the remaining inactive-bank erases and bank
construction. That mandatory window is correctness-bounded by the finite bank
geometry but not claimed to meet a latency target by source arithmetic. Its
device time is a required performance measurement.

A macro upload that itself triggers rotation may still take the open-then-append
shape rather than folding the final 16-KiB candidate into the rotating snapshot.
The engine keeps correctness either way. Optimising that shape requires device
evidence that the difference matters.

## Why An EEPROM Clean Is An Agreed Restart

EEPROM CLEAN is a product-level two-half operation. A serviced pair cannot
clean one half, reboot it, and allow the other half's still-valid persistent
state to repopulate it. Therefore both halves enter storage quarantine and both
must prove a reboot-durable local invalidation before any shared reset deadline
exists.

For ERA NVM, local PREPARE is one result-bearing write of
`EECONFIG_MAGIC_NUMBER_OFF` through
`era_eeprom_driver_prepare_reboot_word()`. PREPARED means more than RAM equality:
the helper calls the same production mount/replay parser and proves an ordinary
next boot recovers MAGIC_OFF from physical flash.

The agreement order is:

```text
REQUEST
  -> storage quarantine
  -> retire Core1 storage publications / admitted episode
  -> local replay-proved MAGIC_OFF prepare on each half
  -> both advertise PREPARED
  -> one common nonzero COMMIT deadline
  -> COMMIT echo/adoption
  -> controlled reset at the agreed deadline
```

A failed NVM prepare publishes no PREPARED vote and creates no commit deadline.
The failure is sticky under the selected CLEAN: quarantine remains monotonic and
the agreement cannot later pretend the failed prepare succeeded.

On the next boot, stock QMK sees MAGIC_OFF and runs `eeconfig_init_quantum()`
(`quantum/eeconfig.c`). Its `nvm_eeconfig_erase()`
(`quantum/nvm/eeprom/nvm_eeconfig.c`) calls `eeprom_driver_format(false)`, which
the ERA custom driver implements as a fresh ERA NVM whole-store format, then
QMK/ERA defaults are rebuilt through ordinary writes. There is no separate
boot-time physical-store wipe algorithm.

ERA NVM's A/B atomicity protects each half's prepare and format against power
loss. The agreed-restart protocol itself guarantees the controlled software
reset sequence; it does not promise simultaneous reset under arbitrary external
RUN/DVDD removal. After any such interruption, each half mounts the last
physically committed ERA NVM state and ordinary relation-open arbitration is the
recovery mechanism.

## Capacity And Publication

The one large permanent ERA NVM allocation is its 24-KiB public image. Split
storage retains one 16-KiB transfer/publication buffer, not an additional
16-KiB old-image copy. Replacement Apply needs no per-slice old-byte scratch.

The host-peer storage core0 state budget and aggregate communication-core static
budget are compile-time equalities, not headroom suggestions. Any struct growth
must move the declared budget and then pass the SRAM residency gate.

Immutable Core1 storage publications remain seqlock/generation fenced. A source
image is never mutated while Core1 is permitted to copy it. CLEAN's quarantine
waits until source publications and result reservations are terminally retired.

A published ready storage result is itself a core0 wake/ownership fact. It is
not conditional on a separately cached runtime-role or result-watch bit. Core1
may finish the first responder exchange in the narrow relation-transition
window before core0 has entered the matching storage runtime role; core0 must
still wake, drain/validate that result, and release its reservation. Otherwise
the one responder-result slot remains occupied, later storage frames are
accepted but cannot reserve a reply result, and the initiator sees a permanent
response-timeout loop from one transient ordering race.

## Scheduler And Matrix Recovery

Storage transfer exclusivity still protects the bulk chunk stream. It ends once
the complete candidate has been validated; the following NVM replacement is a
local synchronous flash window while Core1 continues owning the standing
relation/wire service from SRAM.

No keyboard pass is recursively invoked from ERA NVM. Relation liveness is a
Core1 responsibility during the Core0 flash window. When Apply closes or a
relation identity becomes stale, the existing scheduler recovery path flushes
peer matrix state, forces required response publication and performs status
revalidation/re-proof. It never rolls persistent bytes backward for relation
recovery.

## Diagnostics

Storage diagnostics retain protocol-visible counts: open/close/abort,
proof/match/transfer/chunk/retry/timeout/apply/complete, stale, queue-capacity,
integrity/version/domain/quiesce and source-changed counts. The NVM adapter adds
physical program/erase totals and verified-failure totals.

Retired persistence-mechanism counters are not part of current capture syntax.
`manuals/era_capture_reading.md` owns the remaining console decode; this
contract owns what those values mean for acceptance.

Healthy storage device evidence requires, for the exercised operation:

- no NVM program/erase verification failures;
- no unexpected storage abort/timeout/integrity/stale/queue-expiry or Core1
  failure increase;
- content/readback agreement after close;
- the expected State Sync revision advances exactly at semantic durability;
- relation liveness continues through accepted flash windows.

## What The Lane Costs A Typist

Normal typing is not a storage poll. Local dirty production is a cold cached
mark plus deadline; captures, CRCs, NVM writes and arbitration run only when a
real storage event is due. Core1's standing exchange carries liveness while
Core0 is inside synchronous NVM work.

The two intentionally visible flash costs are a durable local record/Apply and,
when maintenance has fallen behind, mandatory bank rotation. They are measured
on device; source page counts are capacity evidence, not latency claims.
