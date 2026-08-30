# ERA Replacement Storage Contract

Genre: contract
Canonical for: ERA logical EEPROM ownership, ERA NVM durability, the seven-domain
cross-half storage protocol, replacement Apply, dynamic-macro durability, State
Sync revisions, EEPROM CLEAN, storage arbitration and recovery

## Scope And Authority

| Layer | Owner | File |
| --- | --- | --- |
| VIA / eeconfig / dynamic keymap / ordinary QMK features | QMK | public EEPROM API and its logical layout |
| stock QMK EEPROM API | QMK | void write surface |
| `EEPROM_DRIVER=custom` | QMK / ERA boundary | `storage/era_storage_adoption.h`, `storage/era_storage_adoption_rules.mk` |
| custom adapter: mount/init; stock QMK logical reads from the 24-KiB public RAM image; stock QMK writes through the result-bearing NVM engine; committed-span notification; result-bearing remote replacement and CLEAN prepare; one-sector inactive-bank maintenance | ERA | `storage/era_eeprom_driver.c` |
| ERA NVM v1, including whole-store format / erase via `era_nvm_format()` | ERA | `storage/era_nvm.c` |
| RP2040 NOR backend | ERA | `storage/era_nvm_rp2040.c` |

QMK owns the public EEPROM API. ERA owns persistence below it. QMK Core does
not know ERA NVM banks, journal records, flash verification, dirty domains,
replacement transactions, or CLEAN replay proof.

Logical EEPROM is `ERA_STORAGE_EEPROM_LOGICAL_SIZE` / `EEPROM_SIZE` 24576,
asserted as `TOTAL_EEPROM_BYTE_COUNT` 24576 in
`split/era_host_peer_storage.c`. The three split boards
(`sirind/tomak`, `sirind/tomak79h`, `sirind/tomak79s`) include
`storage/era_storage_adoption_rules.mk`, which sets `EEPROM_DRIVER=custom`
and force-includes `storage/era_storage_adoption.h`. QMK wear-leveling is
not linked. An ordinary QMK EEPROM write has no error return. Replacement
Apply and CLEAN never derive durable success from a void QMK call; they use
the result-bearing API below that surface (`storage/era_eeprom_driver.h`).

## ERA NVM Physical Contract

| Item | Value |
| --- | --- |
| effective flash | 2 MiB |
| ERA NVM region | final 128 KiB, excluded from the load image by `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld` |
| banks | A and B, 64 KiB each |
| per-bank layout (`storage/era_nvm_format.h`) | header `0x0000`, activation `0x0100`, 24-KiB snapshot `0x0200`, journal `0x6200` |
| program page | 256 B |
| erase sector | 4 KiB |
| 64-KiB block erase | not required |

At mount, only complete, CRC-valid, format-compatible banks and committed
journal records are accepted. The newest valid bank is authority. A newer
incomplete bank never displaces an older valid bank.

The external power-loss guarantee for one ERA NVM transaction is **old or new,
never a partially public range**: before the record/bank commit authority,
mount recovers the old image; after it, the new image; the public RAM range
changes only after durable commit succeeds.

An append failure seals that tail. A later durable write rotates rather than
reusing an ambiguous slot. Bank construction activates last. Program and erase
are read back immediately; a verification failure is a failed NVM result, never
a successful cache equality. `ERA_NVM_FORMAT_VERSION` 1 is distinct from
`ERA_EEPROM_RESET_KEY`. A physical format change and a logical owner-map change
are separate compatibility decisions.

## Current Storage Inventory

Seven portable domains. Table `g_era_host_peer_storage_domains` in
`split/era_host_peer_storage.c`; ids `era_split_eeprom_sync_domain_t` in
`split/era_split_eeprom_sync.h`; sizes bound across cores by
`ERA_HOST_PEER_STORAGE_DOMAIN_*` in `split/era_host_peer_storage.h` and the
matching `_Static_assert`s in that `.c`. Addresses from
`storage/era_storage_layout.h` and `storage/era_eeprom_layout.h`. Semantic
logical ranges only — never NVM banks, journal, or local metadata.
`ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT` 7. Schema
`ERA_HOST_PEER_STORAGE_SCHEMA_V1` 1 on every row. Probe/proof refuses a
schema or size mismatch before content moves. Both halves of a pair run one
identical image (`era_source_map.md` **Stored-Data Compatibility**).

Reload is `era_split_eeprom_sync_reload_domain_kb()` in
`sirind/common/tomak_common.c`. The weak default in
`split/era_split_eeprom_sync.c` is empty. Keymap and macro have no reload:
QMK reads the public EEPROM image.

| Id | Domain | Address | Size | Reload |
| ---: | --- | --- | --- | --- |
| 0 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG` | `ERA_EEPROM_CONFIG_ADDR` 37 + `ERA_EEPROM_SYNCABLE_CONFIG_OFFSET` 0 (`37..212`) | `ERA_EEPROM_SYNCABLE_CONFIG_SIZE` / `ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES` 176 | board keyboard-config + `era_split_keyboard_reload_features_from_eeprom()` in `split/era_split_keyboard.c` |
| 1 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP` | `ERA_STORAGE_DYNAMIC_KEYMAP_ADDR` / `ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR` 297 | `ERA_HOST_PEER_STORAGE_SCHEMA_DYNAMIC_KEYMAP_LAYERS` 4 × `MATRIX_ROWS` × `MATRIX_COLS` × 2 | none |
| 2 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO` | `ERA_STORAGE_DYNAMIC_MACRO_ADDR` (immediately after keymap) | `ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_MACRO_BYTES` / `ERA_HOST_PEER_STORAGE_IMAGE_BYTES` / `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` 16384 | none. Marker = final byte of this domain |
| 3 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX` | `ERA_STORAGE_EECONFIG_RGB_MATRIX_ADDR` 23 | `ERA_HOST_PEER_STORAGE_DOMAIN_QMK_RGB_MATRIX_BYTES` 8 (`sizeof(rgb_config_t)`) | RGB Matrix |
| 4 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG` | `ERA_STORAGE_EECONFIG_KEYMAP_ADDR` 4 | `ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES` 2 (`sizeof(keymap_config_t)`) | `keymap_config` |
| 5 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER` | `ERA_STORAGE_EECONFIG_DEFAULT_LAYER_ADDR` 3 | `ERA_HOST_PEER_STORAGE_DOMAIN_QMK_DEFAULT_LAYER_BYTES` 1 | default-layer apply |
| 6 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS` | `ERA_STORAGE_VIA_LAYOUT_OPTIONS_ADDR` 296 | `ERA_HOST_PEER_STORAGE_DOMAIN_VIA_LAYOUT_OPTIONS_BYTES` 1 (`VIA_EEPROM_LAYOUT_OPTIONS_SIZE`) | VIA layout-options apply |

QMK prefix `ERA_EEPROM_QMK_CONFIG_SIZE` 37. Portable seats in it are domains
3–5; the rest of that prefix is not a domain. `VIA_EEPROM_MAGIC_ADDR` is
`ERA_EEPROM_CONFIG_END` 293 (three bytes `293..295`). Layout options follow
at 296 because `VIA_EEPROM_LAYOUT_OPTIONS_SIZE` is 1 and
`VIA_EEPROM_CUSTOM_CONFIG_SIZE` is 0. Keymap at 297 is that sum, asserted in
`split/era_host_peer_storage.c`.

Keymap bytes = `ERA_STORAGE_DYNAMIC_KEYMAP_BYTES`. Macro starts at
keymap-base + keymap-bytes. No encoder map sits between them
(`storage/era_storage_layout.h`; `ERA_HOST_PEER_STORAGE_DYNAMIC_ENCODER_ADDR`
in `split/era_host_peer_storage.c` is defined and unused). Every other
domain address and size is geometry-independent.

| Board | Matrix | Keymap | Macro |
| --- | --- | --- | --- |
| `sirind/tomak79h`, `sirind/tomak79s` | 12×9 | 864 at `297..1160` | `1161..17544` |
| `sirind/tomak` | 12×11 | 1056 at `297..1352` | `1353..17736` |

ERA config block: `ERA_EEPROM_CONFIG_SIZE` 256 at `ERA_EEPROM_CONFIG_ADDR`
37, ending at `ERA_EEPROM_CONFIG_END` 293. Interior owner map:
`storage/era_eeprom_layout.h` (abutting `_Static_assert`s, not restated
here). Domain 0 is the syncable half only:
`ERA_EEPROM_SYNCABLE_CONFIG_SIZE` 176, `ERA_EEPROM_SYNCABLE_CONFIG_OFFSET` 0
so the domain image is indexed by config-block offset. Candidate Apply of
domain 0 walks `ERA_EEPROM_SYNCABLE_RESERVED_OFFSET` 164 /
`ERA_EEPROM_SYNCABLE_RESERVED_SIZE` 12 and refuses the domain unless those
bytes are zero (`era_host_peer_storage_reserved_era_config_is_zero()` in
`split/era_host_peer_storage.c`). Protected
`ERA_EEPROM_PROTECTED_CONFIG_OFFSET` 176 /
`ERA_EEPROM_PROTECTED_CONFIG_SIZE` 80 (`176..255` of the block, logical
`213..292`) is not a domain. Local-policy 24-byte layout, version 5, and
protected neighbors: `era_authority_contract.md` **Persisted Sync Policy**.
Rearranging the ERA block still requires the `ERA_EEPROM_RESET_KEY` policy
even when wire schema 1 is unchanged.

> **REFUSED:** growing `ERA_EEPROM_CONFIG_SIZE` past 256, or declaring `EECONFIG_KB_DATA_SIZE` / `EECONFIG_USER_DATA_SIZE` on a storage-adoption board.
> **WHY:** the first moves `VIA_EEPROM_MAGIC_ADDR` and the keymap base; either EECONFIG extra moves `ERA_EEPROM_CONFIG_ADDR` off 37 and the whole schema with it.
> **REOPENS:** a new schema version that relocates VIA magic, keymap, and macro together, with `ERA_EEPROM_RESET_KEY` advanced.

> **REFUSED:** a dynamic encoder map, or any other reserved span, between the keymap and macro domains without a schema change.
> **WHY:** schema 1 places the macro immediately after the keymap (`ERA_STORAGE_DYNAMIC_MACRO_ADDR`); a silent insert would shift the macro on the first board that declared encoders.
> **REOPENS:** a board that declares a dynamic encoder map, with the address formula and schema version changed together.

> **REFUSED:** placing a live `ERA_CONFIG` field inside `ERA_EEPROM_SYNCABLE_RESERVED` without shrinking that reserve.
> **WHY:** the reserved-zero walk refuses the whole domain unless those twelve bytes are zero, so a claim left inside fails every Apply that carries it.
> **REOPENS:** the reserve offset and size shrink in `storage/era_eeprom_layout.h` and the walk follows.

> **REFUSED:** extending domain 0 through the protected range `176..255`.
> **WHY:** those bytes are half-local (sync policy, link level, reset guard, recency baseline) and must be allowed to differ; putting them on the wire would clone arbitration metadata and the link rate.
> **REOPENS:** a design that makes those records identical on both halves without an agreed restart.

## Excluded Local State

Not portable payload, and not in `g_era_host_peer_storage_domains`:

| Span | Content | Home |
| --- | --- | --- |
| rest of the QMK prefix inside `ERA_EEPROM_QMK_CONFIG_SIZE` 37 | magic, debug, and the eeconfig fields that are not domains 3–5 | QMK eeconfig; `storage/era_storage_layout.h` names only the three portable seats |
| `ERA_EEPROM_PROTECTED_CONFIG_OFFSET` 176 / size 80 | local-policy, link, reset guard, recency baseline, protected reserve | `era_authority_contract.md` **Persisted Sync Policy** |
| `ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET` 220 / `ERA_EEPROM_SYNC_BASELINE_CONFIG_SIZE` 32 | seven CRC32 + guard (`220..251`) | **Recency Layer** |
| `ERA_EEPROM_LINK_CONFIG_OFFSET` 200 / size 4 | split link level | `era_authority_contract.md`; `split/era_split_link.h` |
| `ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET` 204 / size 16 | reset guard / `ERA_EEPROM_RESET_KEY` | `storage/era_eeprom_layout.h`; tomak family strict reset |
| `VIA_EEPROM_MAGIC_ADDR` 293, three bytes | VIA magic | QMK VIA |
| after macro to `EEPROM_SIZE` 24576 | unused logical | — |
| ERA NVM banks, generations, journal | physical metadata | `storage/era_nvm.c` |
| runtime transaction generations, route state, diagnostics | not EEPROM | `split/era_host_peer_storage.c` |

The wire synchronizes owner content. It never clones physical storage state
or local arbitration metadata. A recency write uses
`era_eeprom_driver_write_storage_metadata()` in `storage/era_eeprom_driver.c`
and cannot re-dirty a domain: those spans sit outside every domain range.

## Normal Local Writes And Dirty Publication

The authoritative local-write notification is below QMK Core. After a durable
`LOCAL_QMK` commit succeeds, the custom adapter reports the exact changed
logical span to ERA. No QMK `nvm_*` weak hook is part of this boundary.
Dynamic-macro staging is the display-only exception: the NVM transaction mode
opens on the nonzero macro marker and the storage indicator may read that O(1)
RAM fact immediately. **Opening or staging a macro is not a committed-span
notification**: it advances no State Sync revision, starts no settled-dirty
recency claim, and proves no durable content. The ordinary full-domain
committed-span notification happens only after the final zero has committed
successfully.

| Consumer | Effect |
| --- | --- |
| State Sync | classifies the span into KEYMAP, MACRO or CONFIG |
| split storage | marks every overlapping portable domain dirty and starts that domain's trailing quiet deadline |

The trailing quiet interval that gates dirty source capture is
`ERA_HOST_PEER_STORAGE_DIRTY_QUIET_MS` 1000 (`split/era_host_peer_storage.h`).
Repeated writes to one domain replace its deadline and dirty generation. At
settle, one whole-domain capture becomes the immutable source image. There is
no periodic idle patrol.

The accepted 500-ms UI persistence coalescing (`ERA_STORAGE_QUIET_DEFER_MS`
500) is durability/endurance policy, not a second persistence engine. Mechanism
and the QMK-core readers: `era_qmk_fork_ledger.md`. A controlled reset or
suspend flushes an approved pending save before the live lighting object is
changed or discarded.

## Dynamic Macro Transaction

Externally observable upload transcript:

```text
RESET -> FF opener -> payload -> zero close -> targeted marker read
```

The marker is the final byte of the 16-KiB macro domain. Nonzero means the
candidate is invalid/open; zero means the close has committed durably.

| Step | Public image | Durable record | Revision | Indicator |
| ---: | --- | --- | --- | --- |
| 1 | nonzero final-byte write opens staging | none | none | display-only pending arm immediately; `STORAGE_PENDING` / `STORAGE_NEWS` mirrors unfinished work |
| 2 | payload updates the one 24-KiB RAM image | none per write | none | still open |
| 3 | public marker remains nonzero | — | — | QMK cannot execute the staged image |
| 4 | final zero | one whole 16-KiB ERA NVM transaction | none until success | still open |
| 5 | marker zero public only after durable success | committed | MACRO + ordinary dirty/settle before mode returns to IDLE | no close-boundary gap |
| 6 | failed close restores/retains a nonzero marker | none | no MACRO advance | open transaction still reports unfinished work |

The macro transaction gates **only durable writes whose ranges touch the macro
domain**. A keyboard-originated EEPROM write outside that range — including the
deferred RGB Matrix flush that can occur during an upload — remains durable and
survives the next mount. A result-bearing replacement touching the macro domain
while staging is open is `BUSY`/refused.

### Stock QMK macro RESET transcript is a bound dependency

`nvm_dynamic_keymap_macro_reset()` in
`quantum/nvm/eeprom/nvm_dynamic_keymap.c` is recognized without a QMK Core hook.
Stock QMK scans the macro domain sequentially through `eeprom_update_block()`
in `drivers/eeprom/eeprom_driver.c` with a local `uint8_t dummy[16]`.
`era_nvm_qmk_read()` and `era_nvm_qmk_write()` in `storage/era_nvm.c` observe
that exact sequential 16-byte read transcript and stage the corresponding zero
writes as one RESET transaction.

`ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES` 16. `quantum/nvm/eeprom/nvm_dynamic_keymap.c`
must retain the sequential 16-byte stock reset loop unless this recognition
rule is deliberately replaced. `tests/era_nvm_qmk_driver` compiles that stock
QMK source file itself and calls `nvm_dynamic_keymap_macro_reset()`, so a
transcript change fails the test instead of silently disabling RESET. An
unrelated EEPROM read interleaved into that scan aborts recognition.

> **REFUSED:** treat an aligned-all-zero 16-byte write as production RESET recognition.
> **WHY:** the production rule is the sequential 16-byte stock-QMK scan transcript; a second implicit recognizer would accept a different write shape as RESET without replacing that contract.
> **REOPENS:** an explicit replacement of the stock-loop recognition rule, with the test that compiles `quantum/nvm/eeprom/nvm_dynamic_keymap.c` updated to the new transcript.

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

| Revision | Advances when |
| --- | --- |
| KEYMAP | the dynamic keymap/encoder image changed |
| MACRO | a durably completed macro upload or standalone macro RESET only |
| CONFIG | a GET-visible configuration value changed (SET-before-SAVE, an independent durable local write, or successful remote-domain publication) |

These are nonzero wrapping RAM generations, not durable NVM epochs.

**H7S must not copy:** ERA NVM banks, split authority, replacement Apply
internals, or RP2040 flash policy.

## Recency Layer

| Record | Home |
| --- | --- |
| per-domain baseline CRC32 + guard | `ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET` 220 / `ERA_EEPROM_SYNC_BASELINE_CONFIG_SIZE` 32 (`220..251`). Struct `era_host_peer_storage_baseline_record_t` in `split/era_host_peer_storage.c`: seven CRC32 then a guard = CRC32 of those words XOR `ERA_HOST_PEER_STORAGE_BASELINE_GUARD_XOR` |
| 16-bit LE divergence counters | local-policy `ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET` 10, seven × `ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_BYTES` 2; layout `era_authority_contract.md` **Persisted Sync Policy** |

They never cross as portable domain bytes.

| Settled-capture compare | Result |
| --- | --- |
| equal | changed=false; counter becomes zero |
| different | changed=true; counter increments, saturating at `UINT16_MAX` |
| invalid baseline | arbitration degrades conservatively to changed |

An invalid baseline remains conservative across boot: every domain reads as
changed until the relation proves it, so a standalone or unilateral CLEAN is a
real local divergence. Confirmed convergence is one result-bearing replacement
over config-block offsets `ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET` 176 +
`ERA_SPLIT_SYNC_POLICY_STORAGE_COUNTER_OFFSET` 10 through
`ERA_EEPROM_PROTECTED_RESERVED_OFFSET` 252 exclusive (`186..251`) via
`era_eeprom_driver_write_storage_metadata()` in `storage/era_eeprom_driver.c`
(agreed CRC and a zero counter; intervening link/reset bytes copied unchanged). I/O failure leaves both old facts public. First MATCH
after an invalid record keeps unknown neighbour CRC seats at zero. The NVM
origin is internal storage metadata, not `LOCAL_QMK`: no State Sync revision
and no portable dirty-domain notification. MATCH-only relation-open work is
display-provisional.

A failed counter write publishes no new manifest, does not retire the dirty
gate, and advances neither news nor a summary; ERA NVM leaves the public
counter unchanged, so one failure cannot become two advances. A failed
convergence publication retires neither recency fact nor the changed shadow.
A boot with a valid baseline repairs only obviously stale counter state and
does not count the boot as an edit; a failed repair holds the domain behind
the dirty admission gate and retries before any later settled capture. The
repair never advances storage news.

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
old direction. Every serviced relation starts with a mandatory verify-all
audit. An in-session storage-news edge arms a summary but does not force
unchanged domains through new MATCH episodes.

## Storage News Value And Relation-Open Audit

The responder's storage hint is a forward-only nonzero 7-bit news value, not a
domain claim. Each settled divergent capture advances it; zero means no claim.
A new nonzero value tells the peer only to request a fresh summary. The
summary, not the hint, decides domains and direction.

## Relation Admission

The same storage engine serves HOST-PEER and DUAL-HOST. Relation mode changes
only who is admitted as initiator/responder; it does not select a second
storage implementation. Admission requires the existing settled relation
identity, Core1 owner/role, bulk-page capability and EEPROM policy rules. The
responder may remain admitted to answer a policy-closed refusal; the initiator
requires its local EEPROM sync policy requested. Eligibility:
`era_wire_contract.md` and `era_route_contract.md`.

## Source Revision And Identity

A source revision is a nonzero 32-bit immutable-capture generation. CRC32 is
content/integrity identity. They are not interchangeable. An active storage
handoff is fenced by owner epoch, relation generation, request/snapshot
generation, policy generation, storage transaction generation, domain, schema,
chunk id where applicable, source revision, image size and CRC. Generation
zero is invalid. A 16-bit generation is not reused inside one relation;
exhaustion rotates relation identity before returning to one. A source-revision
wrap likewise rotates and invalidates cached manifests before revision one can
be published again.

## Fixed Wire Contract

Markers, bodies, operation ids and eligibility: `era_wire_contract.md`. Core1
owns physical wire execution and copies only from immutable published storage
images. Core0 owns QMK state, domain capture, arbitration, admission, NVM
Apply, runtime reload, manifests and semantic publication.

## Transaction State Machine

### Pull

| Step | Actor | Abort exit |
| ---: | --- | --- |
| 1 | summary / probe | identity / policy / schema refuse |
| 2 | immutable source proof | generation / CRC mismatch |
| 3 | chunk transfer into candidate staging | transfer / timeout / stale |
| 4 | full candidate CRC/schema validation | integrity / domain refuse |
| 5 | `APPLY_READY` exchange | relation / policy change |
| 6 | revalidate every fallible local authority/precondition | any precondition |
| 7 | ADMIT | — |
| 8 | synchronous `era_nvm_replace()` with `REMOTE_APPLY` | NVM failure leaves public RAM old |
| 9 | durable success → runtime reload → immutable manifest + State Sync → COMPLETE / convergence baseline | post-NVM repairs forward |

`era_nvm_replace()` lives in `storage/era_nvm.c`.

### Push Transaction

The initiator publishes its immutable source and opens a push. The responder
stages the acknowledged chunk stream, validates the whole candidate, receives
the apply trigger, revalidates every fallible local precondition, then crosses
the same ADMIT boundary and calls the same synchronous `era_nvm_replace()` in
`storage/era_nvm.c`. After durable success it reloads runtime state, publishes
manifest/State Sync, declares durable, answers COMPLETE and performs the
existing relation close / identity rotation. Direction changes wire roles,
never NVM semantics.

## Replacement Apply: ADMIT And Public Authority

ADMIT is the final cancellation boundary before persistence. Before it, Apply
must revalidate at least:

| Precondition |
| --- |
| domain/schema/image size |
| full candidate CRC |
| `ERA_CONFIG` reserved-zero integrity |
| current relation/owner/role/policy identity |
| peer/source generations applicable to the direction |
| target not dirtied since transfer started |
| manifest/source revision capacity |
| ERA NVM ready state |
| macro-domain availability |

If any precondition fails, no candidate NVM transaction starts. After ADMIT,
relation/policy/source change is **not rollback authority**: the synchronous
NVM call finishes and its own atomicity decides old or new.
`era_nvm_replace()` in `storage/era_nvm.c` publishes the changed public RAM
range only after durable commit succeeds — complete old domain until then,
complete candidate immediately after. No generic alternate EEPROM view. An NVM
failure leaves public RAM old; no success manifest, State Sync revision or
COMPLETE follows. Once NVM succeeds, NVM is canonical and a later
runtime/manifest/Core1 publication failure repairs forward; it never rewrites
EEPROM back to the old candidate solely to repair transport/publication.
Remote Apply uses NVM origin `REMOTE_APPLY` and is not a fresh local dirty
edit. The Apply completion path owns manifest, convergence baseline and State
Sync publication.

## Retry, Duplicate, And Failure Semantics

Wire retry/duplicate handling is unchanged: request and response identities are
generation-matched, duplicate responses are idempotent, and timeouts/stale
results cannot be applied under a new relation.

| Failure class | Effect |
| --- | --- |
| transfer/protocol/identity | no ADMIT, no NVM change |
| NVM program/erase/verification | old public image remains unless commit authority had already made the new image canonical; replay decides |
| post-NVM runtime/publication | canonical NVM remains new; recovery goes forward from it |
| terminal policy/schema/domain refusal | no repeated direction loop without a new event |
| macro-domain busy | remote replacement touching an open macro is refused |

Healthy device acceptance requires `program_failure_count == 0` and
`erase_failure_count == 0`.

## Inactive-Bank Maintenance And Rotation

After mount/rotation, the inactive bank is erased opportunistically by
`era_eeprom_driver_maintenance_task()` in `storage/era_eeprom_driver.c`.
`era_common_features_maintenance_task()` in `system/era_common_features.c`
calls it at top-level housekeeping cadence and each call erases at most one
4-KiB sector. Both ERA class skeletons place that call after their board
presentation tick and yield without erasing while an RGB render-policy refresh
is pending or in progress.

The NVM layer never calls keyboard, matrix, wire or scheduler work from inside
a program/erase primitive. This is deliberately non-recursive. If background
maintenance has not finished when rotation is mandatory, the rotation
synchronously completes the remaining inactive-bank erases and bank
construction. That window is correctness-bounded by the finite bank geometry;
its device time is a required performance measurement, not a source-arithmetic
latency claim. A macro upload that itself triggers rotation may still take the
open-then-append shape; optimising that shape requires device evidence.

> **REFUSED:** reintroduce recursive keyboard/wire work from inside ERA NVM to
> make a long rotation appear interruptible.
> **WHY:** it creates storage re-entry and makes public/runtime authority depend
> on arbitrary action code executed in the middle of a physical transaction.
> **REOPENS:** a separately designed asynchronous NVM transaction contract with
> explicit immutable ownership and recovery, not a callback hidden inside the
> flash backend.

## Why An EEPROM Clean Is An Agreed Restart

EEPROM CLEAN is a product-level two-half operation. A serviced pair cannot
clean one half, reboot it, and allow the other half's still-valid persistent
state to repopulate it. Therefore both halves enter storage quarantine and both
must prove a reboot-durable local invalidation before any shared reset deadline
exists.

For ERA NVM, local PREPARE is one result-bearing write of
`EECONFIG_MAGIC_NUMBER_OFF` through
`era_eeprom_driver_prepare_reboot_word()` in `storage/era_eeprom_driver.c`.
PREPARED means more than RAM equality: the helper calls the same production
mount/replay parser and proves an ordinary next boot recovers MAGIC_OFF from
physical flash.

| Step | Act |
| --- | --- |
| 1 | REQUEST |
| 2 | storage quarantine |
| 3 | retire Core1 storage publications / admitted episode |
| 4 | local replay-proved MAGIC_OFF prepare on each half |
| 5 | both advertise PREPARED |
| 6 | one common nonzero COMMIT deadline |
| 7 | COMMIT echo/adoption |
| 8 | controlled reset at the agreed deadline |

A failed NVM prepare publishes no PREPARED vote and creates no commit deadline.
The failure is sticky under the selected CLEAN. On the next boot, stock QMK
sees MAGIC_OFF and runs `eeconfig_init_quantum()` in `quantum/eeconfig.c`; its
`nvm_eeconfig_erase()` in `quantum/nvm/eeprom/nvm_eeconfig.c` reaches
`eeprom_driver_format(false)`, which the ERA custom driver implements as a
fresh ERA NVM whole-store format, then QMK/ERA defaults rebuild through
ordinary writes. There is no separate boot-time physical-store wipe algorithm.
ERA NVM's A/B atomicity protects each half's prepare and format against power
loss. The agreed-restart protocol does not promise simultaneous reset under
arbitrary external RUN/DVDD removal; after any such interruption, each half
mounts the last physically committed ERA NVM state and ordinary relation-open
arbitration is the recovery mechanism.

## Capacity And Publication

| Figure | Value |
| --- | --- |
| public NVM image | 24 KiB |
| transfer / publication buffer | one `ERA_HOST_PEER_STORAGE_IMAGE_BYTES` 16384; no additional old-image copy |
| host-peer core0 state | `ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES` 140 |
| aggregate communication-core static budget | `ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES` 18524 |

Those two budgets are compile-time equalities. Any struct growth must move the
declared budget and then pass the SRAM residency gate. A source image is never
mutated while Core1 is permitted to copy it. CLEAN's quarantine waits until
source publications and result reservations are terminally retired. Immutable
Core1 storage publications remain seqlock/generation fenced.

A published ready storage result is a core0 wake fact. If no matching
HOST/PEER runtime remains, `era_host_peer_storage_runtime_task()` in
`split/era_host_peer_storage.c` calls
`era_split_communication_core_storage_discard_ready_results()` in
`split/communication_core/era_split_communication_core_storage.c` before any
unserviced early return. A same-role transition may keep the live Core1 lease.
Deferred retirement occupies the one responder-result slot and leaves
`storage_result_due()` asserted: a permanent response-timeout loop or a
permanent core0 housekeeping wake.

## Scheduler And Matrix Recovery

Storage transfer exclusivity protects the bulk chunk stream and ends once the
complete candidate has been validated. The following NVM replacement is a local
synchronous flash window. For a push, the initiator keeps only the standing
runtime cadence suppressed from its final chunk ACK through COMPLETE while the
responder Core0 may be inside that window; storage APPLY/COMPLETE control keeps
running on its dedicated lane. The transfer's already-disabled standing plan
makes this suppression continuous, and the responder snapshot serving the
blocking Apply was published with transfer content suppressed. The responder
general ring has three usable entries, so Core1 coalesces only an exact
successful section-bearing HEARTBEAT already represented by the same immutable
owner/relation/snapshot/section-mask tuple until Core0 drains it. SESSION and
runtime/source-push results stay per-arrival; the remote-Apply gate prevents
those runtime pushes from being originated rather than weakening that rule
(`split/era_host_peer_storage.c`, `split/era_split_transport_scheduler.c`).

No keyboard pass is recursively invoked from ERA NVM. Relation liveness is a
Core1 responsibility during the Core0 flash window. Scheduler recovery never
rolls persistent bytes backward for relation recovery.

## Diagnostics

| Acceptance for the exercised operation | Required |
| --- | --- |
| NVM program/erase verification failures | none |
| unexpected abort/timeout/integrity/stale/queue-expiry or Core1 failure increase | none |
| content/readback after close | agreement |
| State Sync revision | advances exactly at semantic durability |
| relation liveness | continues through accepted flash windows |

Protocol-visible counts and NVM program/erase plus verified-failure totals are
the acceptance surface. Decode: `era_capture_reading.md`. Pending composition
and its consumer live here
(`split/era_host_peer_storage.c`,
`split/communication_core/era_split_communication_core_standing.c`,
`split/scheduler/era_split_transport_scheduler_responder.c`).

| Pending term | Rule |
| --- | --- |
| composition | this half's visible work ∪ the peer's advertised pending mirror ∪ this half's last successfully sent pending=1 |
| confirm | initiator: standing `STORAGE_PENDING` sent state; responder: `STORAGE_NEWS` sent-shadow commit |
| hold / retire | a local fall cannot retire the panel while the peer may still hold the previously confirmed one; a never-crossed one creates no hold; retire on confirmed zero or real service departure |
| local work arm | gated by serviceability and local EEPROM sync policy. Policy off prevents a new sent-one; it does not erase a one the peer already holds |
| boot-only conservative baseline | display-provisional until a transfer/fault proves pair work |
| not a timer | affects no arbitration, persistence, or restart/CLEAN predicate |

The scheduler in `split/era_split_transport_scheduler.c` consumes both
local-carrier confirmations before it runs
`era_host_peer_storage_runtime_task()` in `split/era_host_peer_storage.c`
(responder results, then standing latest-state, then storage runtime).

**Storage recovery does not end an operation merely because the mode label
temporarily reads `LOCAL_NO_LINK`.** The initiator keeps the peer-pending
mirror through the scheduler's fast bootstrap-recovery window; a
bootstrap-backoff miss retires it as a real service departure. Only the
initiator uses that streak-bounded hold
(`split/era_host_peer_storage_indicator_policy.h`,
`split/era_host_peer_storage.c`). After local-clock offset the two logical
edges may differ only by standing poll/housekeeping propagation; **no
opportunistic flash window may then delay one panel's STATUS frame while the
other panel has already changed.** Class skeletons run the board presentation
tick before background NVM maintenance;
`era_common_features_maintenance_task()` in `system/era_common_features.c`
yields while `rgb_matrix_render_policy_refresh_active()` in
`quantum/rgb_matrix/rgb_matrix.c` says the policy edge has not reached PWM
flush. Release authority is a successful local carrier zero, not receipt of
the peer's zero. The `cause` `wire storage ppath` probe is diagnostic only.

## What The Lane Costs A Typist

Normal typing is not a storage poll. Local dirty production is a cold cached
mark plus deadline; captures, CRCs, NVM writes and arbitration run only when a
real storage event is due. Core1's standing exchange carries liveness while
Core0 is inside synchronous NVM work. The two intentionally visible flash costs
are a durable local record/Apply and, when maintenance has fallen behind,
mandatory bank rotation. They are measured on device; source page counts are
capacity evidence, not latency claims.
