# ERA Replacement Storage Contract

Genre: contract
Canonical for: ERA logical EEPROM ownership, ERA NVM durability, the seven-domain
cross-half storage protocol, replacement Apply, dynamic-macro durability, the
`GET_KEYBOARD_VALUE` `0x06` State Sync envelope, EEPROM CLEAN, storage
arbitration and recovery

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

Layout and sizes: `storage/era_nvm_format.h`. Engine: `storage/era_nvm.c`.
RP2040 NOR: `storage/era_nvm_rp2040.c`. Host proofs: `tests/era_nvm`,
`tests/era_nvm_qmk_driver`.

| Item | Value |
| --- | --- |
| effective flash | 2 MiB (`flash1` default `2048k` in `keyboards/era/ld/ERA_RP2040_SRAM_RESIDENT.ld`) |
| ERA NVM region | final 128 KiB: `__era_nvm_region_end__ - 128k`; load-image overlap `ASSERT` |
| banks | `ERA_NVM_BANK_COUNT` 2 × `ERA_NVM_BANK_SIZE_BYTES` 65536 |
| physical | `ERA_NVM_PHYSICAL_SIZE_BYTES` 131072 |
| logical image | `ERA_NVM_LOGICAL_SIZE_BYTES` 24576; public erased byte `0x00` |
| per-bank offsets | header `ERA_NVM_BANK_HEADER_OFFSET` `0x0000`; activation `ERA_NVM_BANK_ACTIVATION_OFFSET` `0x0100`; snapshot `ERA_NVM_BANK_SNAPSHOT_OFFSET` `0x0200`; journal `ERA_NVM_BANK_JOURNAL_OFFSET` `0x6200` |
| journal | `ERA_NVM_BANK_JOURNAL_BYTES` 40448; `_Static_assert` in `storage/era_nvm.c` that one full logical-image record fits |
| program page | `ERA_NVM_PROGRAM_PAGE_BYTES` 256 |
| erase sector | `ERA_NVM_ERASE_SECTOR_BYTES` 4096; 16 sectors/bank |
| 64-KiB block erase | unused. The RP2040 backend issues only one-sector `flash_range_erase` |
| format | `ERA_NVM_FORMAT_VERSION` 1 |
| generations | `ERA_NVM_FIRST_GENERATION` 1 .. `ERA_NVM_MAX_GENERATION` `0xFFFFFFFE` |
| record sequence | `ERA_NVM_FIRST_SEQUENCE` 1 |

Structs (`_Static_assert` sizes): bank header 36 B, activation 24 B, record
header 36 B, trailer 16 B. Magics: bank `ERA_NVM_BANK_MAGIC` `0x314E5245`,
activation `ERA_NVM_ACTIVATION_MAGIC` `0x31564145`, record
`ERA_NVM_RECORD_MAGIC` `0x31525245`, trailer `ERA_NVM_TRAILER_MAGIC`
`0x31545245`. Commit words are programmed last: activation
`ERA_NVM_ACTIVATION_COMMIT` `0x4B4F5641`, record `ERA_NVM_RECORD_COMMIT`
`0x4B4F4352`.

`era_nvm_inspect_bank()` / `era_nvm_select_physical_bank()` accept a bank only
when header, activation, and snapshot CRC all match format 1. The higher valid
generation wins. A newer incomplete bank is not valid and cannot displace an
older valid bank. `era_nvm_replay_bank_range()` then applies committed journal
records in sequence; a torn header, payload CRC, non-erased padding, or trailer
seals that tail (`tail_sealed`).

`era_nvm_mount()` in `storage/era_nvm.c` with no valid bank erases bank 0 as
needed and `era_nvm_construct_bank()` at generation 1. Wear-level-looking
leftover bytes are never parsed or migrated. `era_nvm_format()` is a rotation
whose snapshot is the zero logical image, not a sweep of `era_nvm_replace()`
records. Format is not refused while a macro transaction is open; it clears
the transcript.

`era_nvm_commit_source()` in `storage/era_nvm.c` appends a journal record, or,
if the tail is sealed or the record will not fit, `era_nvm_rotate()` (finish
inactive-bank erase, construct the inactive bank, activation commit last).
`era_nvm_publish_source()` runs only after that durable step succeeds. Ordinary
readers see the complete old range until then, the complete new range
immediately after. An append program/verify failure sets `tail_sealed` and
returns `ERA_NVM_RESULT_IO_ERROR`; the next durable write rotates rather than
reuse the slot. A construction fault before activation leaves the old active
bank untouched and resets `inactive_erase_sector` to 0.

Program and erase go through `era_nvm_flash_program_verified()` /
`era_nvm_flash_erase_verified()`: immediate readback; mismatch increments
`program_failure_count` / `erase_failure_count` and is a failed NVM result,
never a successful cache equality. Healthy device acceptance requires both
failure counters zero.

`ERA_NVM_FORMAT_VERSION` 1 is distinct from `ERA_EEPROM_RESET_KEY`. A physical
format change and a logical owner-map change are separate compatibility
decisions.

Origins (`era_nvm_origin_t` in `storage/era_nvm.h`): `LOCAL_QMK`,
`REMOTE_APPLY`, `MACRO_TRANSACTION`, `CLEAN_PREPARE`, `FORMAT`,
`STORAGE_METADATA`. `era_nvm_replace()` is the result-bearing call for
`REMOTE_APPLY` and CLEAN prepare. It returns `ERA_NVM_RESULT_BUSY` only when
`macro_mode != ERA_NVM_MACRO_IDLE` and the range overlaps the macro domain; a
non-overlapping write stays durable. `ERA_NVM_RESULT_NO_CHANGE` when the
candidate already matches the public image.

RP2040 bind: `era_nvm_rp2040_flash_bind()` requires `ERA_SRAM_RESIDENT_IMAGE`.
Page-program fills unaddressed bytes of the 256 B page with `0xFF`.

> **REFUSED:** a 64-KiB block-erase primitive as a required backend operation.
> **WHY:** production correctness uses only the verified 4-KiB sector; a second geometry would split recovery.
> **REOPENS:** a backend whose only erase size is 64 KiB, with sector accounting rewritten.

> **REFUSED:** treating public-RAM equality, or a void QMK EEPROM write, as durable success.
> **WHY:** Apply and CLEAN have no error return on the QMK surface; only the result-bearing engine plus readback decides old or new.
> **REOPENS:** a QMK EEPROM API that returns program/erase results on this board.

## Stored-Data Compatibility

A split pair runs one identical firmware image; mixed versions are unsupported.
Stored-data backward compatibility is not supported. No compatibility-only ERA
path may read, accept, or convert an earlier stored layout merely to preserve
old stored data. Stale bytes still must not be reinterpreted under a new
layout: any old block that is not valid under the current meaning must fail the
applicable signature/magic/prefix/version guard and be reinitialized rather
than misread. If that fallback cannot be established, compatibility is
unverified and the detector must not be removed.

`ERA_FIRMWARE_VERSION` is compile-time identity only. It is independent of
`ERA_EEPROM_RESET_KEY`, `ERA_NVM_FORMAT_VERSION`, the persisted sync-policy
version, State Sync revisions, and the storage source revision. Those identities
move independently; one changing is not proof that another stale layout is
unreachable. A default-only version change is not a layout change. A normal
UF2 flash does not erase ERA NVM, so same-format data may remain mounted;
physical-format rejection and logical owner-map/reset-key invalidation are
separate decisions.

Two keys pinned in source decide when stored data is wiped; a release that
changes no stored format keeps both, so an ordinary update keeps every setting.

- `ERA_EEPROM_RESET_KEY` (`storage/era_eeprom_layout.h`) is the full-reset key
  of every ERA board, with H7S's name and meaning. The boot reset guard
  (`storage/era_reset_guard.h`) runs before `quantum_init()`; a mismatch
  clears only the eeconfig magic, so the whole logical EEPROM is erased through
  the EEPROM CLEAN path, and the guard is written after the defaults, so a
  reset cut short runs again. Each half of a split pair resets on its own
  first boot, before storage sync. Move the key when stored bytes would mean
  something else and no narrower guard rejects them.
- `QMK_BUILDDATE` (`storage/era_via_magic.h`) seeds VIA's EEPROM magic, whose
  mismatch resets only VIA's region (keymaps, macros, layout options). It is
  pinned for every ERA target and equal to H7S's. Leave it alone: the
  full-reset key also covers keymap geometry.

The ATmega exception (`sirind/brick65`) links no ERA storage and resets only
through QMK's eeconfig and VIA magic.

`tools/era_storage_key.py`, run by the commit hook on the staged index,
fingerprints the sources that define stored bytes against
`tools/era_storage_key.json`. A changed fingerprint fails until a key moves or
the record is rewritten with the reason the stored bytes stay valid; the
record's diff is the review evidence of that decision.

> **REFUSED:** leaving `QMK_BUILDDATE` to the build clock, or making the full-reset guard a per-board option.
> **WHY:** every image built on a new day erased keymaps and macros on its first boot, so an ordinary update could not be told from a format change; a board without the guard silently ignores a raised key.
> **REOPENS:** a VIA magic source other than the date that is still pinned in source.

> **REFUSED:** keep an older-layout reader or converter only for backward storage compatibility.
> **WHY:** the product does not promise stored-data compatibility; a compatibility-only reader creates a second interpretation of persistent bytes, while weakening a stale-layout guard risks silently misreading them.
> **REOPENS:** an explicit requirement to preserve a named older layout, with a versioned reader and tests for both migration and rejection.

### Tap Dance independent timing

Schema 3 allocates `ERA_EEPROM_TAP_DANCE_TIMING_OFFSET` 172, size 24, within
ERA_CONFIG. The packed record contains eight uint16 LE hold thresholds,
one flag byte (bit n enables hold-on-other-key for TDn), version 1, a zero
uint16 reserved field and signature `0x4D544454`. Invalid timing records default
to zero thresholds and flags. Zero follows the existing slot term; 1..65535 ms
is an independent hold threshold. The original 88-byte action/term record stays
at offset 0. The larger block requires a full reset; no old-layout reader exists.
Both halves must run the matching schema-3 image.

Channel 0 values 88..95 encode hold time as BE16, with response byte 2 `0xD3`;
96..103 encode the boolean interruption policy, with response byte 1 `0xD3`.
Mode replies retain byte 1 `0xD2` and advertise advanced timing in byte 2
`0xD3`. GET/SET/SAVE, official VIA range/dropdown controls, State Sync CONFIG
publication, and ERA_CONFIG replacement/reload cover both records.

Modes 1/2 capture both timing values and the interruption flag at the first
press. Released dances use the consecutive-press term, measured from the
preceding press; held dances use the resolved hold threshold. A first hold
that has already resolved cannot start a second-press gesture on release.
With hold-on-other enabled, a pressed first hold in mode 1 or second hold in
modes 1/2 resolves before the other key's layer lookup, only when that hold has
an assigned action (including explicit silence). Mode 2's first press always
remains the physical base key. Default flag 0 retains interruption-as-tap;
mode 0 retains all legacy decisions and ignores advanced timing.

### Tap Dance input modes

Keyboard channel 0 values 80..87 add one mode byte per TD0..TD7 through
existing Custom Value GET/SET/SAVE. GET/SET echo appends support marker `0xD2`;
official V3 definitions expose the same values as dropdowns. Existing action
and term IDs retain their encoding. Invalid modes and conflicting on-press
Hold actions are refused before mutation. CONFIG revision changes only after
the runtime exposes a different setting.

Tap Dance retains the 88-byte version-1 record, slot addresses, signature and
all action/term offsets. Reserved byte 1 tags the input-mode extension with
`0xD2`; bytes 0 and 2 hold two bits per TD0..TD3 and TD4..TD7 respectively,
lowest slot first. Values 0/1/2 mean legacy, after-decision and on-press.
All-zero reserved bytes remain valid mode 0 of the same format. An unknown
tag, nonzero untagged bits, mode 3, or on-press with a non-transparent first
hold is invalid. The mode bits themselves keep the same record format.
Defaults clear the reserved bytes. SAVE persists modes with the existing
actions; SET alone does not promise durability. Older firmware ignores these
mode bits and therefore cannot reproduce the new explicit-silence semantics.

Mode 0 keeps the existing Tap Dance rules, including empty-action fallback.
In modes 1/2, On Tap is the base key. `KC_TRNS` in an additional action means
inherit; `KC_NO` means explicitly send nothing. Hold inherits the base key;
Double Tap inherits two base taps; Tap+Hold inherits a base tap followed by
Hold (or the base key). Each completed pair starts a fresh gesture.

Mode 1 waits only while a different gesture is possible. Without any additional
action it behaves as a normal physical base key. Mode 2 immediately emits and
releases the first base key, including a long first hold. Only the second press
waits: short release chooses Double Tap or one more base tap; expiration while
held chooses Tap+Hold or the base key until release. The initial base input
cannot be suppressed by a later action. Mode 2 requires Hold = `KC_TRNS`;
conflicting mode/action SETs are refused without mutation. Other-key interruption
chooses the base tap path. The existing strict `elapsed > term` boundary and
current per-slot term policy remain. Actions and mode are captured on the first
press so a settings edit cannot change the gesture's output or release owner.

The mode bytes belong to the existing ERA_CONFIG portable domain. Saving and
domain reload use the same storage owner as the actions. Both TOMAK halves
must run the same image; the app cannot claim peer durability from a local SET.

## Current Storage Inventory

Seven portable domains. Table `g_era_host_peer_storage_domains` in
`split/era_host_peer_storage.c`; ids `era_split_eeprom_sync_domain_t` in
`split/era_split_eeprom_sync.h`; sizes bound across cores by
`ERA_HOST_PEER_STORAGE_DOMAIN_*` in `split/era_host_peer_storage.h` and the
matching `_Static_assert`s in that `.c`. Addresses from
`storage/era_storage_layout.h` and `storage/era_eeprom_layout.h`. Semantic
logical ranges only — never NVM banks, journal, or local metadata.
`ERA_SPLIT_EEPROM_SYNC_DOMAIN_COUNT` 7. Schema
`ERA_HOST_PEER_STORAGE_SCHEMA` 3 on every row. The six-layer keymap is unchanged;
schema 3 adds 24 bytes of Tap Dance timing, relocates the protected block and
VIA/keymap/macro bases together, and advances the full-reset key to `0x45524108`. Probe/proof refuses a
schema or size mismatch before content moves. Both halves of a pair run one
identical image; mixed versions are unsupported (**Stored-Data Compatibility**).

Reload is `era_split_eeprom_sync_reload_domain_kb()` in
`sirind/common/tomak_common.c`. The weak default in
`split/era_split_eeprom_sync.c` is empty. Keymap and macro have no reload:
QMK reads the public EEPROM image.

| Id | Domain | Address | Size | Reload |
| ---: | --- | --- | --- | --- |
| 0 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG` | `ERA_EEPROM_CONFIG_ADDR` 37 + `ERA_EEPROM_SYNCABLE_CONFIG_OFFSET` 0 (`37..236`) | `ERA_EEPROM_SYNCABLE_CONFIG_SIZE` / `ERA_HOST_PEER_STORAGE_DOMAIN_ERA_CONFIG_BYTES` 200 | board keyboard-config + `era_split_keyboard_reload_features_from_eeprom()` in `split/era_split_keyboard.c` |
| 1 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP` | `ERA_STORAGE_DYNAMIC_KEYMAP_ADDR` / `ERA_HOST_PEER_STORAGE_DYNAMIC_KEYMAP_ADDR` 321 | `ERA_HOST_PEER_STORAGE_SCHEMA_DYNAMIC_KEYMAP_LAYERS` 6 × `MATRIX_ROWS` × `MATRIX_COLS` × 2 | none |
| 2 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO` | `ERA_STORAGE_DYNAMIC_MACRO_ADDR` (immediately after keymap) | `ERA_HOST_PEER_STORAGE_DOMAIN_DYNAMIC_MACRO_BYTES` / `ERA_HOST_PEER_STORAGE_IMAGE_BYTES` / `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` 16384 | none. Marker = final byte of this domain |
| 3 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX` | `ERA_STORAGE_EECONFIG_RGB_MATRIX_ADDR` 23 | `ERA_HOST_PEER_STORAGE_DOMAIN_QMK_RGB_MATRIX_BYTES` 8 (`sizeof(rgb_config_t)`) | RGB Matrix |
| 4 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG` | `ERA_STORAGE_EECONFIG_KEYMAP_ADDR` 4 | `ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES` 2 (`sizeof(keymap_config_t)`) | `keymap_config` |
| 5 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER` | `ERA_STORAGE_EECONFIG_DEFAULT_LAYER_ADDR` 3 | `ERA_HOST_PEER_STORAGE_DOMAIN_QMK_DEFAULT_LAYER_BYTES` 1 | default-layer apply |
| 6 | `ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS` | `ERA_STORAGE_VIA_LAYOUT_OPTIONS_ADDR` 320 | `ERA_HOST_PEER_STORAGE_DOMAIN_VIA_LAYOUT_OPTIONS_BYTES` 1 (`VIA_EEPROM_LAYOUT_OPTIONS_SIZE`) | VIA layout-options apply |

QMK prefix `ERA_EEPROM_QMK_CONFIG_SIZE` 37. Portable seats in it are domains
3–5; the rest of that prefix is not a domain. `VIA_EEPROM_MAGIC_ADDR` is
`ERA_EEPROM_CONFIG_END` 317 (three bytes `317..319`). Layout options follow
at 320 because `VIA_EEPROM_LAYOUT_OPTIONS_SIZE` is 1 and
`VIA_EEPROM_CUSTOM_CONFIG_SIZE` is 0. Keymap at 321 is that sum, asserted in
`split/era_host_peer_storage.c`.

Keymap bytes = `ERA_STORAGE_DYNAMIC_KEYMAP_BYTES`. Macro starts at
keymap-base + keymap-bytes. No encoder map sits between them
(`storage/era_storage_layout.h`; `ERA_HOST_PEER_STORAGE_DYNAMIC_ENCODER_ADDR`
in `split/era_host_peer_storage.c` is defined and unused). Every other
domain address and size is geometry-independent.

| Board | Matrix | Keymap | Macro |
| --- | --- | --- | --- |
| `sirind/tomak79h`, `sirind/tomak79s` | 12×9 | 1296 at `321..1616` | `1617..18000` |
| `sirind/tomak` | 12×11 | 1584 at `321..1904` | `1905..18288` |

ERA config block: `ERA_EEPROM_CONFIG_SIZE` 280 at `ERA_EEPROM_CONFIG_ADDR`
37, ending at `ERA_EEPROM_CONFIG_END` 317. Interior owner map:
`storage/era_eeprom_layout.h` (abutting `_Static_assert`s, not restated
here). Domain 0 is the syncable half only:
`ERA_EEPROM_SYNCABLE_CONFIG_SIZE` 200, `ERA_EEPROM_SYNCABLE_CONFIG_OFFSET` 0
so the domain image is indexed by config-block offset. Candidate Apply of
domain 0 walks `ERA_EEPROM_SYNCABLE_RESERVED_OFFSET` 196 /
`ERA_EEPROM_SYNCABLE_RESERVED_SIZE` 4 and refuses the domain unless those
bytes are zero (`era_host_peer_storage_reserved_era_config_is_zero()` in
`split/era_host_peer_storage.c`). Protected
`ERA_EEPROM_PROTECTED_CONFIG_OFFSET` 200 /
`ERA_EEPROM_PROTECTED_CONFIG_SIZE` 80 (`200..279` of the block, logical
`237..316`) is not a domain. Local-policy 24-byte layout, version 5, and
protected neighbors: `era_authority_contract.md` **Persisted Sync Policy**.
Rearranging the ERA block still requires the `ERA_EEPROM_RESET_KEY` policy
even when the wire schema is unchanged.

> **REFUSED:** growing `ERA_EEPROM_CONFIG_SIZE` past 280, or declaring `EECONFIG_KB_DATA_SIZE` / `EECONFIG_USER_DATA_SIZE` on a storage-adoption board.
> **WHY:** the first moves `VIA_EEPROM_MAGIC_ADDR` and the keymap base; either EECONFIG extra moves `ERA_EEPROM_CONFIG_ADDR` off 37 and the whole schema with it.
> **REOPENS:** a new schema version that relocates VIA magic, keymap, and macro together, with `ERA_EEPROM_RESET_KEY` advanced.

> **REFUSED:** a dynamic encoder map, or any other reserved span, between the keymap and macro domains without a schema change.
> **WHY:** the schema places the macro immediately after the keymap (`ERA_STORAGE_DYNAMIC_MACRO_ADDR`); a silent insert would shift the macro on the first board that declared encoders.
> **REOPENS:** a board that declares a dynamic encoder map, with the address formula and schema version changed together.

> **REFUSED:** placing a live `ERA_CONFIG` field inside `ERA_EEPROM_SYNCABLE_RESERVED` without shrinking that reserve.
> **WHY:** the reserved-zero walk refuses the whole domain unless those four bytes are zero, so a claim left inside fails every Apply that carries it.
> **REOPENS:** the reserve offset and size shrink in `storage/era_eeprom_layout.h` and the walk follows.

> **REFUSED:** extending domain 0 through the protected range `200..279`.
> **WHY:** those bytes are half-local (sync policy, link level, reset guard, recency baseline) and must be allowed to differ; putting them on the wire would clone arbitration metadata and the link rate.
> **REOPENS:** a design that makes those records identical on both halves without an agreed restart.

## Excluded Local State

Not portable payload, and not in `g_era_host_peer_storage_domains`:

| Span | Content | Home |
| --- | --- | --- |
| rest of the QMK prefix inside `ERA_EEPROM_QMK_CONFIG_SIZE` 37 | magic, debug, and the eeconfig fields that are not domains 3–5 | QMK eeconfig; `storage/era_storage_layout.h` names only the three portable seats |
| `ERA_EEPROM_PROTECTED_CONFIG_OFFSET` 200 / size 80 | local-policy, link, reset guard, recency baseline, protected reserve | `era_authority_contract.md` **Persisted Sync Policy** |
| `ERA_EEPROM_SYNC_BASELINE_CONFIG_OFFSET` 244 / `ERA_EEPROM_SYNC_BASELINE_CONFIG_SIZE` 32 | seven CRC32 + guard (`244..275`) | **Recency Layer** |
| `ERA_EEPROM_LINK_CONFIG_OFFSET` 224 / size 4 | split link level | `era_authority_contract.md`; `split/era_split_link.h` |
| `ERA_EEPROM_RESET_GUARD_CONFIG_OFFSET` 228 / size 16 | reset guard / `ERA_EEPROM_RESET_KEY` | `storage/era_eeprom_layout.h`; every board, `storage/era_reset_guard.c` |
| `VIA_EEPROM_MAGIC_ADDR` 317, three bytes | VIA magic | QMK VIA |
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

An open macro's RAM staging bytes are not a durable snapshot source. The
16-KiB domain's final byte is the marker: nonzero means the transaction is open
and QMK must not execute the staged image. Staging may mutate the one 24-KiB
public RAM image, but it creates no durable macro record, State Sync revision,
or portable dirty claim.

Externally observable upload transcript:

```text
RESET -> FF opener -> payload -> zero close -> targeted marker read
```

The final zero is accepted only after payload was seen. It commits one
whole-domain `MACRO_TRANSACTION` record; marker zero and the ordinary committed
span become public only after that durable commit succeeds. A failed close
keeps the marker invalid and the previous complete macro mount-recoverable.
Reads, unrelated writes, headroom repair, and bank rotation must preserve that
last complete durable macro without a second 16-KiB staging image. If the
durable source cannot be read while constructing a replacement bank, that bank
must not activate or publish. `CLOSE`, `RESET`, and `FORMAT` supply complete
replacements. `tests/era_nvm` and `tests/era_nvm_qmk_driver` prove the
production and QMK-facing paths.

A zero close with no payload, or opener/payload without an open transaction, is
`ERA_NVM_RESULT_PROTOCOL`. A durable write outside the macro domain remains
allowed during upload; replacement that overlaps the open macro domain is
`ERA_NVM_RESULT_BUSY`.

### Stock QMK macro RESET transcript is a bound dependency

Production RESET recognition is bound to QMK's sequential 16-byte
`eeprom_update_block()` scan in `nvm_dynamic_keymap_macro_reset()` from
`quantum/nvm/eeprom/nvm_dynamic_keymap.c`;
`ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES` is 16. An already-zero whole scan is
`ERA_NVM_RESULT_NO_CHANGE`; an interleaved unrelated EEPROM read aborts
recognition; a failed RESET leaves the marker invalid. The stock loop must
retain this transcript unless this contract and `tests/era_nvm_qmk_driver`,
which compiles the stock QMK source, are changed together.

> **REFUSED:** treat an aligned-all-zero 16-byte write as production RESET recognition.
> **WHY:** the production rule is the sequential 16-byte stock-QMK scan transcript; a second implicit recognizer would accept a different write shape as RESET without replacing that contract.
> **REOPENS:** an explicit replacement of the stock-loop recognition rule, with the test that compiles `quantum/nvm/eeprom/nvm_dynamic_keymap.c` updated to the new transcript.

## External H7S State-Sync And Macro Handoff

This is the QMK/ERA producer contract for the existing 32-byte VIA RAW HID
surface, not an H7S implementation plan. The source owner is
`system/era_state_sync.[ch]`, compiled only under `VIA_ENABLE`.

The envelope reuses `id_get_keyboard_value` `0x02` with
`ERA_STATE_SYNC_KEYBOARD_VALUE` `0x06`; SET selector `0x06` remains unclaimed.
`ERA_STATE_SYNC_ENVELOPE_VERSION` is `0x01`. No new command id, report,
transaction id, capability bit, or `BUSY` status is added.
`VIA_PROTOCOL_VERSION` remains `0x000C` and is a separate protocol version. A
handled envelope is 32 bytes and reports RAM revisions only; the GET path does
not read EEPROM, CRC, or NVM.

QMK VIA protocol 13 (QMK release 2026-05-31) gives `id_get_keyboard_value`
`0x06` to `id_keycodes_version`, which official VIA reads from protocol-13
boards before remapping. `via_command_kb` runs before the QMK core, so this
envelope would shadow it. Keep `0x000C` until the envelope moves to a new
selector together with the app
(`the-via-eerraa/docs/adr/0001-state-sync-protocol.md`); a QMK merge that
brings protocol 13 must not land before that. Shipped firmware keeps `0x06`.

### Envelope v1

`id_get_keyboard_value` (`0x02`) + `ERA_STATE_SYNC_KEYBOARD_VALUE` (`0x06`) +
`ERA_STATE_SYNC_ENVELOPE_VERSION` (`0x01`). Integers on the wire are big-endian.

Request (`system/era_state_sync.c`; names in `system/era_state_sync.h`):

| Byte | Meaning |
| ---: | --- |
| `0` | `id_get_keyboard_value` `0x02` |
| `1` | `ERA_STATE_SYNC_KEYBOARD_VALUE` `0x06` |
| `2` | request envelope version |
| `3` | must be `0` |
| `4..5` | opaque tag; copied back unchanged; this tree does not interpret them as an integer |
| `6..31` | must be `0` |

Response after the handler zeros bytes `2..31`, writes version `0x01`, and
restores the tag:

| Byte | Meaning |
| ---: | --- |
| `0` | `0x02` |
| `1` | `0x06` |
| `2` | `ERA_STATE_SYNC_ENVELOPE_VERSION` `0x01` (firmware writes this, not the request version byte) |
| `3` | `ERA_STATE_SYNC_STATUS_OK` `0x00`, `ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION` `0x01`, or `ERA_STATE_SYNC_STATUS_INVALID` `0x02` |
| `4..5` | echoed tag |
| `6` | OK writes `ERA_STATE_SYNC_DOMAIN_MASK_INITIAL` (`0x07`); error leaves the byte zero |
| `7` | `0` |
| `8..11` | OK only: KEYMAP revision, BE32 |
| `12..15` | OK only: MACRO revision, BE32 |
| `16..19` | OK only: CONFIG revision, BE32 |
| `20..31` | `0` |

Domain bits: `ERA_STATE_SYNC_DOMAIN_KEYMAP` `0x01`,
`ERA_STATE_SYNC_DOMAIN_MACRO` `0x02`, `ERA_STATE_SYNC_DOMAIN_CONFIG` `0x04`.
Version is checked before reserved bytes. A request version other than `0x01`
is `UNSUPPORTED_VERSION` even if reserved bytes are also nonzero. A matching
version with a nonzero reserved byte (`3` or `6..31`) is `INVALID`. On both
error statuses the tag is still echoed and revisions are not filled. GET does
not bump any revision.

`tests/era_via_exact_ms/test_exact_ms.cpp` covers the OK envelope, reserved
`INVALID`, unsupported version, tag echo, and the `length` 31 false return.

> **REFUSED:** `BUSY`, extra status codes, a second report shape, a transaction
> id, a capability bit, or a new command id on this selector.
> **WHY:** v1 is GET `0x02` selector `0x06`, statuses `0`/`1`/`2`, one 32-byte
> layout; a second shape would be another protocol.
> **REOPENS:** a new envelope version, approved on this tree.

### `length` and short packets

Only a full 32-byte payload is a v1 envelope. A direct handler call with
`length < 32` returns unhandled and emits no State Sync envelope;
`tests/era_via_exact_ms/test_exact_ms.cpp` covers the length-31 case. The current
ChibiOS RAW dispatcher calls this producer with `RAW_EPSIZE` 32, so transport-
specific short-OUT handling is not an envelope rule. A consumer must treat any
non-32-byte IN as no v1 envelope, never `INVALID` or a fourth status.

### Revisions

KEYMAP, MACRO, and CONFIG are RAM `uint32` tokens, each starting at 1 and
skipping zero on wrap. They are neither NVM generations nor the storage source
revision.

| Token | Advances when |
| --- | --- |
| KEYMAP | a durable local write changes the dynamic-keymap span, or remote Apply durably replaces the dynamic-keymap domain |
| MACRO | the dynamic-macro transaction closes durably, or remote Apply durably replaces the macro domain; opener/payload staging does not advance it |
| CONFIG | a semantic SET-before-SAVE commits a configuration change (the matching later persist is suppressed as a duplicate), or an independent durable change/remote Apply reaches syncable ERA config, RGB Matrix, `keymap_config`, default layer, or VIA layout options |

Protected ERA config, recency/storage metadata, `CLEAN_PREPARE`, `FORMAT`, an
open macro's staging writes, and a no-op persist do not advance these tokens.
Remote Apply advances the named token only after durable replacement succeeds;
it is not also reported as a fresh local dirty edit.

> **REFUSED:** H7S copying ERA NVM banks, split authority, replacement Apply
> internals, or RP2040 flash policy.
> **WHY:** this selector carries three RAM generations on the existing VIA GET;
> those surfaces are not on the envelope and are not a portable domain.
> **REOPENS:** a new envelope version that names those surfaces, approved on
> this tree and both peers together.

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
over config-block offsets `ERA_EEPROM_LOCAL_POLICY_CONFIG_OFFSET` 200 +
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

The settled counter belongs to local storage truth and survives relation
rotation. The consumed peer-news value resets at the scheduler's rotation
boundary, before new-generation news can be accepted. Core1 retires its own
received cache on the owner/relation identity; Core0 never concurrently clears
that publication. The consumed value does **not** reset at audit startup:
housekeeping may already have drained current-generation standing news before
storage runtime starts its audit. Policy-only audits also preserve it.
Replaying an unchanged value after an all-MATCH audit must neither arm another
summary nor light the indicator. A genuinely new relation may reuse the value.

News that arrives while the reopened relation still owes its verify-all audit
is consumed into that audit and arms no in-session summary
(`era_host_peer_storage_note_host_news()` in `split/era_host_peer_storage.c`,
gated by the relation's audit-owed mark set at boot and at rotation and
cleared when the audit begins). The audit's summary re-derives every domain,
so a visible summary armed ahead of it would be a false pending edge on both
panels — and the audit may wait behind a link agreement for many passes.

The relation/policy audit receipt is independent of the active transaction's
identity. A durable close adopted across recovery cannot mark the reopened
relation as already audited. Its mandatory audit starts once the runtime is
idle and the link is settled; transaction stamps authorize transactions only.

**Summary request and active episode are separate obligations.** Taking a
summary consumes its inbox bit before the request is sent. The domainless
active episode remains outstanding through completion, with the existing
provisional-audit display rule. Later news can independently re-arm the inbox;
an old result retires only its episode, never that later request. Recoverable
summary abort restores an inbox request. A terminal refusal does not retry
forever and discards obsolete direction queues it could not reclassify.

New summary work revokes a selected but unsent domain token. An accepted
whole-family summary replaces all direction masks rather than unioning them
with old decisions. A SOURCE_CHANGED quiet deadline also returns through a
summary, never a remembered pull. Only fresh classification or its current
conflict counter result may choose a direction.

An incoming result must match the pending request's complete identity before
it retires that request. Stale identities and exact duplicate completions
cannot clear a different request's pending latch.

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

## Transaction Semantics

The runtime state enum is source-owned by `era_host_peer_storage_runtime_state_t`
in `split/era_host_peer_storage.c`; operation ids are source-owned by
`era_split_eeprom_sync_op_t` in `split/era_split_eeprom_sync.h`. Pull and push
share one storage meaning; direction changes wire roles, not NVM semantics, and
the half receiving the candidate is the Apply target.

Every replacement follows the same required boundaries:

1. probe/proof fixes domain, schema, size, source revision and content CRC;
2. transfer completes an immutable full candidate and verifies its CRC;
3. `APPLY_READY` means the accepted transaction may reach Core0 Apply; it is
   **not durable success**;
4. the current relation/policy/source facts are revalidated at the final ADMIT
   boundary;
5. after ADMIT, the result-bearing NVM call decides complete old or complete new;
6. only durable NVM success permits reload, State Sync publication and
   convergence bookkeeping; a later transport/close failure never rolls the
   persistent bytes backward.

`ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE` protects only the bulk
transfer and ends once the candidate is transfer-verified; Apply/COMPLETE use
the compact control path. A proof/open MATCH is the symmetric no-transfer
short circuit. Detailed states and operations are recovered from the source
enums and `split/communication_core/era_split_communication_core_storage.c`.

## LINK SPEED and restart storage ordering

LINK's four-byte record remains outside EEPROM SYNC. Its durable writes belong
to the checked LINK act only after physical runtime readiness; the complete
local success/failure/reboot rules remain in `split/era_split_link.h`.

An admitted storage episode, including verify-only audit, and any outstanding
result prevent a new LINK arm because the audit may still discover an Apply.
Queued boot-audit work alone does not. During an already timed LINK arm, new
cold capture/initiator admission yields while active/context-loss retirement
still runs; CLEAN's prepared quarantine retains its own retirement path.

A final program/readback failure can leave a new complete record physically
committed while public RAM still exposes the last confirmed value. Such an
append or bank-construction failure is sealed recovery state: a RAM-identical
replacement must establish the requested durable image by checked rotation
before it can report `NO_CHANGE`. Power loss before a checked result may recover
either complete image, never a promise of pair-wide durability.

## Replacement Apply: ADMIT And Public Authority

ADMIT is the last cancellation boundary. Before it, the implementation must
revalidate the pinned relation/owner/role and policy/source identity (including
the pull-side host generations), complete size/CRC, domain-0 reserved-zero
rule, target-not-dirty/not-stale state, and publication/revision capacity.
Schema/size refusal happens earlier at probe/proof; an overlapping open macro
may still return `ERA_NVM_RESULT_BUSY`. The current source predicate is
`era_host_peer_storage_apply_commit()` in `split/era_host_peer_storage.c`.

`APPLY_READY` or any other accepted wire/runtime result is not durable success.
Only `ERA_NVM_RESULT_OK` or `ERA_NVM_RESULT_NO_CHANGE` from the result-bearing
replacement is durable success. Other results publish no reload, manifest,
State Sync revision, or COMPLETE.

After ADMIT, a relation, policy, or source change has no rollback authority.
The synchronous NVM operation decides old-or-new; public EEPROM remains the
complete old domain until durable commit and becomes the complete candidate
after it. If later runtime/publication work fails, recovery repairs forward from
the canonical NVM image and never rewrites the old candidate. Remote Apply is
not a fresh local dirty edit. Push records convergence at durable success; pull
records convergence only after `CLOSE_RSP` `COMPLETE`.

> **REFUSED:** a post-ADMIT relation, policy, or source change as authority to rewrite the old domain.
> **WHY:** the public/durable boundary is the NVM commit; rolling bytes back to repair transport would invent a second EEPROM view.
> **REOPENS:** an asynchronous NVM transaction with an explicit immutable rollback record.

## Retry, Duplicate, And Failure Semantics

Wire retry/duplicate handling is unchanged: request and response identities are
generation-matched, duplicate responses are idempotent except the push
`COMPLETE` poll, which must not replay a provisional `APPLY_READY`
(`split/communication_core/era_split_communication_core_storage.c`).
Timeouts/stale results cannot be applied under a new relation.

Episode bound `ERA_HOST_PEER_STORAGE_EPISODE_MS` 5000, re-armed only when the
state **or** pinned operation advances (`era_host_peer_storage_note_episode_phase()`
in `split/era_host_peer_storage.c`). Repeating `APPLY_READY` does not re-arm.
Retry spacing `ERA_HOST_PEER_STORAGE_RETRY_MS` 25, capped by
`ERA_HOST_PEER_STORAGE_MAX_FAILURES`.

| Failure class | Effect |
| --- | --- |
| transfer/protocol/identity | no ADMIT, no NVM change |
| NVM program/erase/verification | old public image remains unless commit authority had already made the new image canonical; replay decides |
| post-NVM runtime/publication | canonical NVM remains new; recovery goes forward from it |
| terminal policy/schema/domain refusal | no repeated direction loop without a new event |
| macro-domain busy | remote replacement touching an open macro is `BUSY` |

Healthy device acceptance requires `program_failure_count == 0` and
`erase_failure_count == 0`.

## Inactive-Bank Maintenance And Rotation

Background maintenance may erase at most one inactive 4-KiB sector per call
and must yield while the board presentation refresh is active. If rotation is
required before maintenance finishes, ERA NVM synchronously completes the
remaining sectors and constructs the inactive bank; correctness is bounded by
the finite 16-sector bank, but wall time is device evidence and must not be
claimed from source arithmetic. A macro-triggered rotation remains subject to
the same rule.

Program/erase primitives must not call keyboard, matrix, wire, or scheduler
work. Source owners are `storage/era_nvm.c`, `storage/era_eeprom_driver.c`,
and `system/era_common_features.c`.

> **REFUSED:** reintroduce recursive keyboard/wire work from inside ERA NVM to make a long rotation appear interruptible.
> **WHY:** it creates storage re-entry and makes public/runtime authority depend on arbitrary action code executed in the middle of a physical transaction.
> **REOPENS:** a separately designed asynchronous NVM transaction contract with explicit immutable ownership and recovery, not a callback hidden inside the flash backend.

## Why An EEPROM Clean Is An Agreed Restart

Act `ERA_SPLIT_RESTART_ACT_EEPROM_CLEAN` 2 in
`split/era_split_restart_agreement.h`. Rules in `split/era_split_keyboard.c`:
`requires_confirmation`, `yields_to_storage`, `resets`; user `param_max` 0.
Protocol phases occupy the same two-bit param:
`ERA_SPLIT_RESTART_CLEAN_PARAM_REQUEST` 0,
`ERA_SPLIT_RESTART_CLEAN_PARAM_PREPARED` 1,
`ERA_SPLIT_RESTART_CLEAN_PARAM_COMMIT` 2.
AUTHORITY admits unarmed REQUEST and armed PREPARED or COMMIT. RESTART_ARM
admits PREPARED with `commit_ms` 0 or COMMIT with nonzero `commit_ms`. Markers
and bodies: `era_wire_contract.md`.

A serviced pair cannot clean one half, reboot it, and allow the other half's
still-valid persistent state to repopulate it. Both halves therefore enter
storage quarantine and both must prove a reboot-durable local invalidation
before any shared reset deadline exists.

VIA three-confirm on a split board calls
`era_via_system_eeprom_clean_handed_off()` in `split/era_split_keyboard.c`,
which `era_split_restart_agreement_request()`s the act and returns true so the
local path does not also reset. The weak default in `system/era_via_system.c`
returns false: a non-split board waits
`era_via_system_restart_quiet_ok()`, then `era_via_system_eeprom_invalidate()`,
then `soft_reset_keyboard()`.

Local PREPARE is `era_split_restart_prepare_local()` in
`split/era_split_keyboard.c` → `era_via_system_eeprom_invalidate()` in
`system/era_via_system.c` →
`era_eeprom_driver_prepare_reboot_word()` in `storage/era_eeprom_driver.c`:
one `era_nvm_replace()` of `EECONFIG_MAGIC_NUMBER_OFF` at
`ERA_STORAGE_EECONFIG_MAGIC_ADDR` 0, origin `CLEAN_PREPARE`, then
`era_nvm_replay_read()` of the same word through the production mount/replay
parser. `ERA_NVM_RESULT_OK` and `ERA_NVM_RESULT_NO_CHANGE` are success;
mismatch is `ERA_NVM_RESULT_IO_ERROR`. PREPARED is that replay proof, not RAM
equality.

| Step | Act |
| ---: | --- |
| 1 | REQUEST (AUTHORITY unarmed; raw-HID quiet gate on the commanding half) |
| 2 | initiator selects: `clean_selected`; `era_split_restart_agreement_storage_quarantined()` true |
| 3 | PREPARE arm, `commit_ms` 0; responder receipt also sets `clean_selected` |
| 4 | `era_host_peer_storage_restart_quarantine_ready()` in `split/era_host_peer_storage.c`: wait until `runtime_service_active` and `active_due` are clear, then `era_split_communication_core_storage_retire_publications()` in `split/communication_core/era_split_communication_core_storage.c` |
| 5 | each half's replay-proved MAGIC_OFF; advertise PREPARED |
| 6 | both PREPARED and `era_split_restart_arm_ready()` (time-anchor adopted or this half is the clock source) → COMMIT arm at `sync_timer_read32() + ERA_SPLIT_RESTART_COMMIT_DELAY_MS` 120 |
| 7 | responder adopts COMMIT only if remaining time is in `(0, 120]`; COMMIT echo |
| 8 | `soft_reset_keyboard()` at the agreed deadline |

A failed NVM prepare sets `clean_prepare_failed` (sticky under this CLEAN),
publishes no PREPARED vote, and creates no commit deadline. Quarantine remains
true so a half whose boot predicate may already be OFF cannot re-advertise its
old portable image.

Standalone (no serviced relation): same quarantine and replay proof, then
immediate reset — no peer phase and no shared-clock deadline. Promotion into
bilateral roll-forward is monotonic if a peer becomes serviced before reset.

On the next boot, stock QMK sees MAGIC_OFF (`nvm_eeconfig_is_disabled()` in
`quantum/nvm/eeprom/nvm_eeconfig.c`) and runs `eeconfig_init_quantum()` in
`quantum/eeconfig.c`; `nvm_eeconfig_erase()` reaches
`eeprom_driver_format(false)`, which `storage/era_eeprom_driver.c` implements
as `era_nvm_format()`. There is no separate boot-time physical-store wipe.
ERA NVM A/B atomicity protects each half's prepare and format against power
loss. The agreed-restart protocol does not promise simultaneous reset under
arbitrary external RUN/DVDD removal; after any such interruption, each half
mounts the last physically committed ERA NVM state and ordinary relation-open
arbitration is the recovery mechanism.

Arm timeout `ERA_SPLIT_RESTART_ARM_TIMEOUT_MS` 60. Unanswered COMMIT retires
the arm and waits for a PREPARED answer observed after that idle before a
fresh COMMIT. Request lifetime `ERA_SPLIT_RESTART_REQUEST_LIFETIME_MS` 5000.

> **REFUSED:** a one-half CLEAN that leaves the peer's still-valid store able to restore the old image at reopen.
> **WHY:** relation-open arbitration would treat the CLEANed half as the empty side of a divergence and copy the uncleansed image back.
> **REOPENS:** a design in which CLEAN is not a pair-visible storage invalidation.

> **REFUSED:** publishing PREPARED from public-RAM equality without `era_nvm_replay_read()`.
> **WHY:** PREPARED is the claim that an ordinary next boot recovers MAGIC_OFF from flash; RAM equality is not that proof.
> **REOPENS:** a prepare path whose only success is a production mount of a second image.

> **REFUSED:** creating T_commit before both halves have advertised replay-proved PREPARED.
> **WHY:** a deadline with only one durable vote is a one-half reset with a clock on it.
> **REOPENS:** none while CLEAN still requires bilateral physical invalidation.

## Capacity And Publication

| Figure | Value |
| --- | --- |
| public NVM image | 24 KiB |
| transfer / publication buffer | one `ERA_HOST_PEER_STORAGE_IMAGE_BYTES` 16384; no additional old-image copy |
| host-peer core0 state | `ERA_HOST_PEER_STORAGE_CORE0_STATE_BYTES` 144 |
| aggregate communication-core static budget | `ERA_HOST_PEER_STORAGE_STATIC_BUDGET_BYTES` 18528 |
| Core0 storage state | 144 bytes; includes an independent 4-byte relation/policy audit receipt. Local news uses existing local-state padding |
| cause-instrumented storage aggregate | 20144 bytes; exact target assertion, not an increase to the SRAM free-floor allowance |

Those two budgets are compile-time equalities. Any struct growth must move the
declared budget and then pass the SRAM residency gate. A source image is never
mutated while Core1 is permitted to copy it. CLEAN's quarantine waits until
source publications and result reservations are terminally retired. Immutable
Core1 storage publications remain seqlock/generation fenced.

A published ready storage result remains a Core0 wake obligation until matching
runtime consumes it or context loss terminally discards it. A same-role
transition may retain the live Core1 lease; deferred retirement occupies the
single responder-result slot and must keep `storage_result_due()` asserted
rather than turn a lost consumer into a permanent timeout/housekeeping loop.

## Scheduler And Matrix Recovery

Storage-specific scheduler requirements are only these: route exclusivity
protects the bulk transfer, Apply/COMPLETE remain runnable on the storage
control lane after transfer verification, NVM replacement is synchronous
Core0 work, and Core1's standing exchange carries relation liveness across the
flash window. Storage recovery never rolls persistent bytes backward.
Cadence/suppression composition is owned by `era_route_contract.md`; source
anchors are `split/era_host_peer_storage.c` and
`split/era_host_peer_storage_standing_policy.h`.

## Diagnostics

| Acceptance for the exercised operation | Required |
| --- | --- |
| NVM program/erase verification failures | none |
| unexpected abort/timeout/integrity/stale/queue-expiry or Core1 failure increase | none |
| content/readback after close | agreement |
| State Sync revision | advances exactly at semantic durability |
| relation liveness | continues through accepted flash windows |

Protocol-visible counts and verified NVM program/erase failure totals are the
acceptance surface; decode lives in `era_capture_reading.md`.

The storage-pending indication is presentation only. It composes local visible
work, the peer's advertised pending mirror, and the last successfully sent
pending-one. A local fall cannot retire an indication while the peer may still
hold that confirmed one; retirement needs confirmed zero or real service
departure. Policy-off prevents arming a new local one but does not erase a
peer-held confirmation. The boot-only conservative baseline remains
provisional, and none of these indication facts may change arbitration,
persistence, or restart/CLEAN predicates.

A transient `LOCAL_NO_LINK` label inside fast bootstrap recovery is not by
itself operation completion. After clock alignment, opportunistic flash
maintenance must not delay one panel's storage STATUS edge after the other has
changed; presentation runs before background NVM maintenance and maintenance
yields to an active refresh. Release authority is the successful local carrier
zero, not receipt of the peer's zero. The host-testable policy owner is
`split/era_host_peer_storage_indicator_policy.h`; the `cause` `wire storage
ppath` probe remains diagnostic only.

## What The Lane Costs A Typist

Normal typing is not a storage poll. Local dirty production is a cold cached
mark plus deadline; captures, CRCs, NVM writes and arbitration run only when a
real storage event is due. Core1's standing exchange carries liveness while
Core0 is inside synchronous NVM work. The two intentionally visible flash costs
are a durable local record/Apply and, when maintenance has fallen behind,
mandatory bank rotation. They are measured on device; source page counts are
capacity evidence, not latency claims.
