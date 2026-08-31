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

`ERA_NVM_FORMAT_VERSION` 1 is distinct from `ERA_EEPROM_RESET_KEY`
`0x45524104`. A physical format change and a logical owner-map change are
separate compatibility decisions.

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

Marker = final byte of the 16-KiB domain (`ERA_NVM_DYNAMIC_MACRO_SIZE_BYTES`
16384). Engine: `era_nvm_qmk_write()` / `era_nvm_macro_qmk_write()` in
`storage/era_nvm.c`. Modes `ERA_NVM_MACRO_IDLE`, `ERA_NVM_MACRO_WRITE_OPEN`,
`ERA_NVM_MACRO_RESET_OPEN`. Staging mutates the one 24-KiB public image while
the marker stays nonzero. QMK will not execute a nonzero-marker image.

Externally observable upload transcript:

```text
RESET -> FF opener -> payload -> zero close -> targeted marker read
```

| Step | Public image | Durable record (`storage/era_nvm.c`) | State Sync / dirty |
| ---: | --- | --- | --- |
| 1 | nonzero final-byte write; `era_nvm_ensure_macro_headroom()` first | none | none. Display-only pending arm may rise |
| 2 | payload copies into the same RAM image | none per write | none |
| 3 | marker held nonzero (`leave_zero_marker_invalid`) | — | still open |
| 4 | final zero after `macro_payload_seen` | one whole-domain `era_nvm_commit_source()` origin `MACRO_TRANSACTION` | none until success |
| 5 | marker 0 published only after durable success | committed | MACRO + ordinary committed-span; mode returns to `IDLE` |
| 6 | failed close keeps marker `0xFF`; old durable image mount-recoverable | none | no MACRO advance |

A zero close with no payload seen is `ERA_NVM_RESULT_PROTOCOL` and leaves the
marker `0xFF`. Opener/payload without a prior open is `PROTOCOL`. The macro
transaction gates **only durable writes whose ranges touch the macro domain**.
A keyboard-originated EEPROM write outside that range — including the deferred
RGB Matrix flush during an upload — goes through `era_nvm_replace()` in
`storage/era_nvm.c` origin `LOCAL_QMK` and survives the next mount.
`era_nvm_replace()` overlapping the macro domain while staging is open returns
`ERA_NVM_RESULT_BUSY`.

### Stock QMK macro RESET transcript is a bound dependency

`nvm_dynamic_keymap_macro_reset()` in
`quantum/nvm/eeprom/nvm_dynamic_keymap.c` loops `eeprom_update_block()` on a
local `uint8_t dummy[16]`. `era_nvm_qmk_read()` in `storage/era_nvm.c`
recognizes that sequential 16-byte scan without a QMK Core hook; the matching
zero writes then become one RESET transaction. Already-zero whole scan:
`ERA_NVM_RESULT_NO_CHANGE`, no durable write. Failed RESET leaves the marker
invalid; a later payload write may resume `WRITE_OPEN`. An unrelated EEPROM
read interleaved into the scan aborts recognition
(`era_nvm_macro_reset_abort_scan()`).

`ERA_NVM_QMK_MACRO_RESET_CHUNK_BYTES` 16.
`quantum/nvm/eeprom/nvm_dynamic_keymap.c` must retain the sequential 16-byte
stock reset loop unless this recognition rule is deliberately replaced.
`tests/era_nvm_qmk_driver` compiles that stock QMK source file and calls
`nvm_dynamic_keymap_macro_reset()`.

> **REFUSED:** treat an aligned-all-zero 16-byte write as production RESET recognition.
> **WHY:** the production rule is the sequential 16-byte stock-QMK scan transcript; a second implicit recognizer would accept a different write shape as RESET without replacing that contract.
> **REOPENS:** an explicit replacement of the stock-loop recognition rule, with the test that compiles `quantum/nvm/eeprom/nvm_dynamic_keymap.c` updated to the new transcript.

## External H7S State-Sync And Macro Handoff

This is the QMK/ERA producer for the existing 32-byte VIA RAW HID surface.
Producer: `system/era_state_sync.c`, compiled only under `VIA_ENABLE`
(`system/era_common_qmk_rules.mk`). Entry is `via_command_kb()` in
`system/era_via_system.c` when `ERA_VIA_SYSTEM_ENABLE`, else the same symbol in
`system/era_state_sync.c`. Both call `era_state_sync_via_command()`. It is not
an H7S implementation plan.

No new command id, report, transaction id, capability bit, or BUSY status is
added. `VIA_PROTOCOL_VERSION` in `quantum/via.h` is `0x000C`. Stock GET
selectors are `id_uptime` `0x01` .. `id_firmware_version` `0x04`; SET-only
`id_device_indication` is `0x05`. Selector `ERA_STATE_SYNC_KEYBOARD_VALUE`
`0x06` is unused on that stock GET/SET table. `id_set_keyboard_value` with
selector `0x06` is not claimed by `era_state_sync_via_command()`; `quantum/via.c`
marks `id_unhandled` `0xFF`.

### Envelope v1

`id_get_keyboard_value` (`0x02`) + `ERA_STATE_SYNC_KEYBOARD_VALUE` (`0x06`) +
`ERA_STATE_SYNC_ENVELOPE_VERSION` (`0x01`). Integers on the wire are big-endian.
`era_state_sync_via_command()` in `system/era_state_sync.c` always
`raw_hid_send()`s 32 bytes when it handles the report. The GET path does not
read EEPROM, CRC, or NVM.

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

### `length` and short packets (this tree)

`era_state_sync_via_command()` in `system/era_state_sync.c` returns false with
no `raw_hid_send()` when `data` is null, `length` is less than 2, the command
is not GET selector `0x06`, or `length` is less than 32. Returning false leaves
the report to `raw_hid_receive()` in `quantum/via.c`, which for an unknown GET
selector writes `id_unhandled` `0xFF` and then `raw_hid_send()`s `data` with
the same `length`. `send_raw_hid()` in `tmk_core/protocol/chibios/usb_main.c`
drops a send when `length != RAW_EPSIZE`.

On the USB RAW path that `length` is not the host OUT byte count.
`raw_hid_task()` in `tmk_core/protocol/chibios/usb_main.c` calls
`raw_hid_receive(buffer, sizeof(buffer))` with `RAW_EPSIZE` 32
(`tmk_core/protocol/usb_descriptor.h`). The same fact is written on the custom
value rail in `system/era_common_via.h`: a short OUT still arrives as 32 with
the tail of an uninitialised stack buffer. A reserved-byte nonzero tail is
therefore `INVALID` `0x02` on that path, not a `length < 32` miss. The C
`length < 32` arm is what `tests/era_via_exact_ms/test_exact_ms.cpp` exercises;
the USB path does not pass a `length` other than 32.

### Peer observation (not edited, not decided here)

Byte layout of the 32-byte v1 envelope matches this tree on the VIA ADR 0001
and H7S `contract_via` §5 texts read for this re-measure. Peers document bytes
`4..5` as BE16; this tree echoes the two bytes without combining them.

Short-packet semantics do not match. This table records both sides and does
not pick a winner.

| Side | What it documents or does with `length < 32` |
| --- | --- |
| this tree, `era_state_sync_via_command()` | returns false; no send from this unit (`system/era_state_sync.c`; test above) |
| this tree, USB RAW | `length` is always 32; short OUT is not visible as `length < 32`; garbage reserved bytes are `INVALID` |
| this tree, VIA fallback after false | `quantum/via.c` writes `0xFF`; `send_raw_hid()` then drops a non-32 `length` |
| H7S `contract_via` §5 | not `INVALID`; handler returns false; `via.c` writes `id_unhandled` `0xFF`; buffer is not rewritten as a v1 envelope. REFUSED answering `INVALID` on `length < 32`. H7S TX is `via_hid_task` enqueue of the same 32-byte buffer; H7S `raw_hid_send()` is an empty stub |
| VIA ADR 0001 | `0xFF` is not an envelope; probe `0xFF` is the unhandled-selector fallback. H7S §5's peer note: app `parseStateSyncEnvelope` returns null when the IN length is not 32 (neither `0xFF` nor `INVALID`) |

H7S previously documented `INVALID` for this case; current H7S `contract_via`
§5 documents `0xFF`.

### Revisions

Three RAM `uint32` tokens in `system/era_state_sync.c`, each starting at 1.
`era_state_sync_next()` skips 0 on wrap. They are not NVM generations and not
the wire source-revision.

| Token | Advances when (`system/era_state_sync.c`) |
| --- | --- |
| KEYMAP | `era_state_sync_note_eeprom_span()` overlaps `[ERA_STORAGE_DYNAMIC_KEYMAP_ADDR, ERA_STORAGE_DYNAMIC_MACRO_ADDR)`, or `era_state_sync_note_storage_domain()` for `ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP`. Schema 1 has no encoder map in that span |
| MACRO | `era_state_sync_note_eeprom_span()` overlaps the 16-KiB macro domain, or `era_state_sync_note_storage_domain()` for `ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO`. The custom adapter emits that span only after a completed macro transaction; opener/payload staging emits nothing. Transcript: **Dynamic Macro Transaction** |
| CONFIG | SET-before-SAVE via `era_state_sync_note_config_semantic_commit()`; a later matching `era_eeprom_update_config()` persist in `storage/era_eeprom_config_io.c` is suppressed for that region only (`era_state_sync_config_persist_begin()` / `_end()`). Independent durable span: syncable ERA config (`ERA_EEPROM_SYNCABLE_CONFIG_OFFSET` 0 / size 176) except the suppressed region, plus RGB Matrix, `keymap_config`, default layer, and VIA layout options. Remote Apply: `era_state_sync_note_storage_domain()` for ERA_CONFIG, QMK_RGB_MATRIX, QMK_KEYMAP_CONFIG, QMK_DEFAULT_LAYER, VIA_LAYOUT_OPTIONS |

Does not advance: protected ERA config, recency/storage-metadata origin,
`REMOTE_APPLY` / `CLEAN_PREPARE` / `FORMAT` through
`era_eeprom_driver_note_commit()` in `storage/era_eeprom_driver.c` (those
origins return before notification; remote Apply uses
`era_state_sync_note_storage_domain()` in `split/era_host_peer_storage.c`
after durable success), an open macro's staging writes, and a no-op persist
of a region already published by SET-before-SAVE.

On a storage-adoption board, `era_eeprom_driver_note_commit()` forwards
`LOCAL_QMK` and `MACRO_TRANSACTION` through
`era_host_peer_storage_note_eeprom_commit()` in `split/era_host_peer_storage.c`,
which calls `era_state_sync_note_eeprom_span()`. Without the storage engine,
the same notifier calls `era_state_sync_note_eeprom_span()` directly.

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

Runtime states: `era_host_peer_storage_runtime_state_t` in
`split/era_host_peer_storage.c`. Ops: `era_split_eeprom_sync_op_t` in
`split/era_split_eeprom_sync.h`. Core1 snapshot answers
`ERA_SPLIT_EEPROM_SYNC_STATUS_APPLY_READY` in
`split/communication_core/era_split_communication_core_storage.c`. There is no
`APPLY_WRITE` slice state: `era_nvm_replace()` is one synchronous old-or-new
call. Direction changes wire roles, never NVM semantics. The apply target is
the half that receives the candidate.

`ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE` ends at transfer-verified:
pull, when staged bytes equal image size and the full CRC matches; push, when
the last chunk ACK advances to apply-wait. Apply/COMPLETE are compact control.

### Pull

Initiator states `PEER_PROBE` → `PEER_TRANSFER` → `PEER_APPLY` →
(local ADMIT + NVM) → `PEER_REVALIDATE` → `PEER_COMPLETE`.

| Step | State / op | Abort |
| ---: | --- | --- |
| 1 | `PEER_PROBE` / `PROBE_REQ` | identity / policy / schema / size refuse |
| 2 | `PROOF_RSP` MATCH (done) or TRANSFER | generation / CRC mismatch |
| 3 | `PEER_TRANSFER` / `CHUNK_REQ` into candidate staging | transfer / timeout / stale / `SOURCE_CHANGED` |
| 4 | full candidate CRC vs proof | `INTEGRITY_FAIL`; exclusivity drops |
| 5 | `PEER_APPLY` / `APPLY_REQ`; `APPLY_RSP` `APPLY_READY` | relation / policy / source change |
| 6 | `era_host_peer_storage_apply_commit()` revalidation | any precondition below; no NVM |
| 7 | ADMIT | — |
| 8 | `era_eeprom_driver_replace()` origin `REMOTE_APPLY` → `era_nvm_replace()` in `storage/era_nvm.c` | NVM failure leaves public RAM old (`BUSY` or `INTEGRITY_FAIL`) |
| 9 | `era_split_eeprom_sync_reload_domain_kb()`; manifest; `era_state_sync_note_storage_domain()` | post-NVM publication repairs forward; no EEPROM rewrite |
| 10 | `PEER_REVALIDATE`: same peer usb/open/close generations and policy; wait while `status_revalidation_due` or `general_initiator_pending` | identity change tears the episode; NVM already new |
| 11 | `PEER_COMPLETE` / `COMPLETE_REQ`; `CLOSE_RSP` `COMPLETE` then `era_host_peer_storage_note_domain_converged()` | stale close aborts the episode, not the NVM image |

### Push Transaction

Initiator: `PEER_PUSH_OPEN` / `PUSH_CTL` `OPEN` → `PEER_PUSH_CHUNKS` /
`PUSH_CHUNK_REQ` → `PEER_PUSH_APPLY` / `PUSH_CTL` `APPLY` →
`PEER_PUSH_COMPLETE` / `PUSH_CTL` `COMPLETE`.

Responder: `HOST_PUSH_OPEN` → `HOST_PUSH_STAGING` → `HOST_PUSH_APPLY_WAIT` →
on Core1 `APPLY_READY`, the same `era_host_peer_storage_apply_commit()`
(`host_push` true) → `HOST_PUSH_DURABLE`. Convergence baseline is written at
that durable declaration. After `COMPLETE`,
`era_split_transport_scheduler_rotate_storage_relation()`.

Open compare MATCH is the pull-symmetric short circuit: nothing to move.

## Replacement Apply: ADMIT And Public Authority

ADMIT is the last cancellation boundary inside
`era_host_peer_storage_apply_commit()` in `split/era_host_peer_storage.c`.
The comment at that predicate is the fence: no cancellation check belongs
below it. Core1 may advance relation facts during the flash window; that
affects COMPLETE/re-proof, not rollback.

Revalidated before ADMIT:

| Check (`split/era_host_peer_storage.c`) | Source |
| --- | --- |
| `era_host_peer_storage_context_host()` or `_peer()` | current relation/owner/role |
| `owner_epoch`, `relation_generation`, `policy_generation` | pinned vs live |
| pull only: `peer_usb_epoch`, `peer_host_open_generation`, `peer_host_close_generation` | same |
| staging complete; `image_size` == domain descriptor size | domain table |
| full candidate CRC32 == `expected_crc32` | `era_split_wire_crc32()` |
| domain 0: `era_host_peer_storage_reserved_era_config_is_zero()` | reserved-zero walk |
| `TARGET_DIRTY` clear and `image_stale` clear | not dirtied since transfer |
| `era_host_peer_storage_apply_publication_preflight()` | `era_eeprom_driver_ready()`; domain id; even `image_publication_seq` with room; `source_revision_counter != UINT32_MAX`; no `revision_wrap_pending` |

Schema/size mismatch is refused at probe/proof, not re-decoded here. Macro-domain
availability is `era_nvm_replace()` returning `ERA_NVM_RESULT_BUSY`.

If any row fails, no candidate NVM transaction starts. After ADMIT the
function clears `ROUTE_EXCLUSIVE` and calls `era_eeprom_driver_replace()`.
Relation/policy/source change is **not rollback authority**: the synchronous
NVM call finishes and its own atomicity decides old or new.
`era_nvm_replace()` in `storage/era_nvm.c` publishes the changed public RAM
range only after durable commit succeeds — complete old domain until then,
complete candidate immediately after. No generic alternate EEPROM view.

NVM `OK` or `NO_CHANGE` is durable success. Any other result: `BUSY` maps to
`ERA_SPLIT_EEPROM_SYNC_STATUS_BUSY`, else `INTEGRITY_FAIL`; no reload, no
manifest, no State Sync, no COMPLETE. Once NVM succeeds, NVM is canonical. A
later `era_host_peer_storage_publish_current_image()` failure in
`split/era_host_peer_storage.c` refills the staging buffer from
`eeprom_read_block()` in `storage/era_eeprom_driver.c` and retries; a second
failure marks `image_stale` and returns `STALE`. It never writes the old
candidate back to EEPROM. Remote Apply origin `REMOTE_APPLY` is not a fresh
local dirty edit (`era_eeprom_driver_note_commit()` ignores it).

Push writes the convergence baseline at durable success. Pull writes it on
`CLOSE_RSP` `COMPLETE`.

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

After mount/rotation, `era_nvm_scan_inactive_erase_prefix()` in
`storage/era_nvm.c` counts already-erased inactive sectors.
`era_eeprom_driver_maintenance_task()` in `storage/era_eeprom_driver.c` erases
at most one 4-KiB sector. `era_common_features_maintenance_task()` in
`system/era_common_features.c` is the caller. Both class skeletons place that
call after the board presentation tick: `split/era_split_board.c` and
`system/era_nonsplit_board.c`. The maintenance function returns without erasing
while `rgb_matrix_render_policy_refresh_active()` in
`quantum/rgb_matrix/rgb_matrix.c` is true.

The NVM layer never calls keyboard, matrix, wire or scheduler work from inside
a program/erase primitive. If background maintenance has not finished when
rotation is mandatory, `era_nvm_finish_inactive_erase()` in `storage/era_nvm.c`
synchronously completes the remaining inactive-bank erases, then bank
construction. That window is
correctness-bounded by the finite 16-sector bank; its device time is a required
performance measurement, not a source-arithmetic latency claim. A macro upload
that itself triggers rotation may still take the open-then-append shape;
optimising that shape requires device evidence.

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

`ERA_HOST_PEER_STORAGE_RUNTIME_FLAG_ROUTE_EXCLUSIVE` protects the bulk chunk
stream and clears at transfer-verified (Pull/Push tables above). The NVM
replacement that follows is one synchronous Core0 call in
`era_host_peer_storage_apply_commit()` (`split/era_host_peer_storage.c`).
Push-initiator standing suppression after exclusivity:
`era_host_peer_storage_standing_suppressed()` in that file — route-exclusive
OR (initiator AND (`PEER_PUSH_APPLY` or `PEER_PUSH_COMPLETE`)); composition
`era_route_contract.md`. APPLY/COMPLETE stay on the dedicated storage lane.

ERA NVM program/erase primitives do not call keyboard, matrix, wire, or
scheduler work (`storage/era_nvm.c`). Relation liveness during the Core0 flash
window is Core1's standing exchange (`era_invariants.md`). Scheduler recovery
never rolls persistent bytes backward for relation recovery.

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
