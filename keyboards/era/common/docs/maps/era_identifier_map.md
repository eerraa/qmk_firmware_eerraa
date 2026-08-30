# ERA Identifier Map

Genre: map
Canonical for: the identifier space no header owns — retired numbers that stay
un-reused, VIA keyboard-channel bands 0..79, GET_KEYBOARD_VALUE 0x06, feature
channels 9..15, sync and link value ids, storage status ids, and the USB-session
threshold that lives in a .c — plus the mode, route, and payload facts a capture
still decodes against

**A retired identifier's number is never recycled.** Captured console lines are
dated by what their numbers meant when they were taken. Every "retired, stays
allocated" row below is that rule.

Decoding a console line is `era_capture_reading.md`'s. This file names the
source identifier; that file names the capture column.

Recomputed from `storage/era_eeprom_layout.h`, `system/era_via_system.h`,
`system/era_common_via.c`, `features/era_tapdance_via.c`,
`features/era_tapping_via.c`, `features/era_socd_via.c`,
`features/era_kkuk_via.c`, `features/era_mousekey_via.c`,
`features/era_debounce_via.c`, `split/era_split_via_sync.h`,
`split/era_split_via_link.h`, and the status enum in
`split/era_split_eeprom_sync.h`. `era_doc_refs.py` does not recompute these
tables. Firmware answers both the exact-ms term ids (`72..79`, channel 15
id 5) and the legacy term ids (`36,41,46,51,56,61,66,71`, channel 15 id 1).

## Modes

`split/era_split_mode_planner.h` names the live set: `LOCAL_NO_LINK` 0,
`HOST_PEER_HOST` 1, `HOST_PEER_PEER` 2, `DUAL_HOST_LEFT` 3, `DUAL_HOST_RIGHT` 4.
Values 5 and 6 were `HOST_HOST_LEFT`/`HOST_HOST_RIGHT`. The header no longer
holds them reserved; 5 is the next relation's if one is added.

## Routes And Route Reasons

`split/era_split_wire_router.h` names both enums, each with `_NONE = 0`. Live
kinds: `HOST_PEER_HEARTBEAT`, `HOST_PEER_SOURCE_PUSH`, `ATTACH_STATUS`. Live
reasons: `ATTACH_STATUS_REVALIDATION`, `HOST_PEER_MATRIX_SOURCE_PUSH`,
`RUNTIME_SECTION_PUSH`, `RUNTIME_RESPONSE_POLL`. The unproduced
`HOST_PEER_LIVENESS` reason is gone from the enum. What each live reason runs
is `era_route_contract.md`. Every live value has a producer.

**Read `rr`, not `rk`, to tell two runtime routes apart.**
`ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT` stays at its value. Core1's standing
service stamps that kind into transaction timing for both live runtime
reasons (`split/communication_core/era_split_communication_core_standing.c`),
so a HOST-PEER capture reads the heartbeat kind for traffic that is not the
heartbeat route.

> **REFUSED:** add `HOST_PEER_STORAGE`, `HOST_PEER_STORAGE_IDLE_PROOF`, or
> `HOST_PEER_STORAGE_ACTIVE` to `era_split_wire_router.h`.
> **WHY:** those names are contract-level service causes only; storage is a
> dedicated cold-task lane, not an owner route kind (`era_route_contract.md`).
> **REOPENS:** the route contract admits a general owner-route kind for storage.

## Storage Service Causes

`HOST_PEER_STORAGE`, `HOST_PEER_STORAGE_IDLE_PROOF`, and
`HOST_PEER_STORAGE_ACTIVE` have **no C enumerator**. Probes are not a periodic
patrol. Writers of `probe_pending_mask` are the summary cell — including every
later `SUMMARY_STATUS` result — and the conflict cell;
`era_host_peer_storage_note_host_news()` in `split/era_host_peer_storage.c`
does not touch that mask.

## Payloads

`split/era_split_wire_protocol.h` names payload classes, HOST-PEER op ids,
HSRSP mask bits, visual-resync reasons, and RGB/mask/time-anchor widths.
`split/era_split_eeprom_sync.h` names storage ops `0xE0..0xEE` (`0xEF`
reserved), push phases, and the seven domains.

Retired payload kind values, un-reused: `4` was `ERROR_NACK` (class `0x40`);
`6` was `DUAL_HOST`. Every op byte the split layer writes is `0x10`, `0x20`,
`0x21`, or `0xE0..0xEF`. Class `0x40` falls to the `default` reject.

Control-only semantic names, not class/op identifiers:
`HOST_PEER_HEARTBEAT`, `HOST_PEER_ACK_STATUS`.

### Storage status ids

`era_split_eeprom_sync_status_t` in `split/era_split_eeprom_sync.h`. Payload
shapes: `era_host_peer_storage_contract.md`.

| Id | Name |
| ---: | --- |
| 0 | `MATCH` |
| 1 | `TRANSFER` |
| 2 | `APPLY_READY` |
| 3 | `COMPLETE` |
| 4 | `ABORTED` |
| 5 | `POLICY_CLOSED` |
| 6 | `UNSUPPORTED_DOMAIN` |
| 7 | `UNSUPPORTED_SCHEMA` |
| 8 | `SIZE_MISMATCH` |
| 9 | `STALE` |
| 10 | `BUSY` |
| 11 | `INTEGRITY_FAIL` |
| 12 | `RESULT_FULL` |
| 13 | `TIMEOUT` |
| 14 | `ROLE_CHANGED` |
| 15 | `SOURCE_CHANGED` |

`ERA_SPLIT_EEPROM_SYNC_STATUS_RESULT_FULL` 12 has no wire producer. The
communication-core name `ERA_SPLIT_COMMUNICATION_CORE_STORAGE_REQUEST_RESULT_FULL`
is a different enum in `split/communication_core/era_split_communication_core_storage.h`.

> **REFUSED:** delete `RESULT_FULL` from the status enum.
> **WHY:** it sits in a numbered sequence; deleting it would renumber
> `TIMEOUT`, `ROLE_CHANGED`, and `SOURCE_CHANGED`.
> **REOPENS:** a wire-format bump that can retire the hole without moving live
> ids.

## VIA Keyboard-Channel Value IDs

Channel `id_custom_channel` 0 (`quantum/via.h`).
`era_common_via_handle_keyboard_channel_command()` in `system/era_common_via.c`
runs common claimants before the board hook in `system/era_board_hooks.c`.

| Band | Owner | Ids |
| --- | --- | --- |
| `0..3` | PWM backlight, only where `ERA_BACKLIGHT_EFFECT_ENABLE` | 0 brightness, 1 effect, 2 breathing period, 3 blink speed (`features/era_backlight_via.h`) |
| `0..4` | otherwise the board hook | tomak: 0 lock-indicator mode, 1 override, 2 brightness, 3 colour, 4 badge-only (`sirind/common/tomak_common.h`). odessey: 1 select, 2 brightness, 3 colour, 4 Velocikey (`newone/common/odessey_common.h`; no id 0) |
| `5` | NKRO | `ERA_VIA_NKRO_ENABLE_VALUE_ID` 5 (`features/era_nkro_via.h`) |
| `6..12` | RGB Matrix lock indicators, only where `ERA_RGB_INDICATOR_ENABLE` | 6 master enable, then source/brightness/colour per slot: 7..9 slot 1, 10..12 slot 2 (`features/era_rgb_indicator_via.h`). A one-slot board answers 6..9 and declines 10..12 |
| `32..71` | eight tap-dance slots, stride 5 | base 32, four actions then legacy 1-byte/10 ms term at field 4: ids `36,41,46,51,56,61,66,71` (`features/era_tapdance_via.c`; `ERA_TAP_DANCE_SLOT_COUNT` 8, `ERA_TAP_DANCE_ACTION_COUNT` 4 in `features/era_tapdance.h`) |
| `72..79` | exact-ms tap-dance terms TD0–TD7 | additive 2-byte big-endian milliseconds; does not reuse the legacy term ids |

**The `0..3` overlap is real and is held apart by the selector, not by the
numbers.** Backlight ids are what the shipped backlight-board definitions
address. No board compiles both `ERA_BACKLIGHT_EFFECT_ENABLE` and a
keyboard-channel handler of its own. The common router runs first, so on a
board that enabled both, ids 0..3 would go to backlight.

> **REFUSED:** place RGB-indicator ids at `4..6` as an extension of odessey's
> `1..3`.
> **WHY:** that draft put slot-2 brightness on 5, which is the NKRO toggle,
> answered by the common router before the board hook, so the slider would have
> toggled NKRO.
> **REOPENS:** NKRO leaves id 5, or the keyboard-channel router inverts so the
> board hook runs first.

`6..12` overlaps nothing. A new claimant on this channel checks the whole
table, not the band it was aiming at.

A one-slot RGB-indicator board's menus may omit id 6; firmware still answers
it. Backlight id 2's shipped JSON label is `Breating Period`.

## VIA GET_KEYBOARD_VALUE selectors

Command `id_get_keyboard_value` `0x02` (`quantum/via.h`). Stock selectors:
`0x01` uptime, `0x02` layout options, `0x03` switch matrix, `0x04` firmware
version on GET; `0x05` device indication on SET.
ERA's polling-first revision query is selector **`0x06`**
(`ERA_STATE_SYNC_KEYBOARD_VALUE` in `system/era_state_sync.h`).
`via_command_kb()` in `system/era_via_system.c` stamps raw-HID quiet and fully
handles only that selector, via `era_state_sync_via_command()` in
`system/era_state_sync.c`. It is not a custom-channel value id. Envelope
version `0x01`; byte-3 statuses 0 OK, 1 unsupported version, 2 invalid. Exact
envelope: `era_host_peer_storage_contract.md`.

## VIA Feature Channels

VIA itself reserves `0..5` (`quantum/via.h`: custom, qmk backlight, rgblight,
rgb matrix, audio, led matrix). ERA uses `9..15`. `6..8` are idle.

| Channel | Handler | Value ids |
| ---: | --- | --- |
| `ERA_VIA_SYSTEM_CHANNEL` 9 (`system/era_via_system.h`) | `era_via_system_handle_via_command()` in `system/era_via_system.c`, then sync/link on the same channel | 1 bootloader, 2..4 EEPROM-clean confirms; 5..7 sync; 8..9 link |
| `ERA_VIA_SOCD_LR_CHANNEL` 10 (`storage/era_eeprom_layout.h`) | `era_socd_handle_via_command()` in `features/era_socd_via.c` | 1 enable, 2 keycode 0, 3 keycode 1, 4 mode |
| `ERA_VIA_SOCD_UD_CHANNEL` 11 | same handler, Up/Down pair | same four ids |
| `ERA_VIA_KKUK_CHANNEL` 12 | `era_kkuk_handle_via_command()` in `features/era_kkuk_via.c` | 1 enable, 2 delay, 3 repeat, 4 mode |
| `ERA_VIA_MOUSEKEY_CHANNEL` 13 | `era_mousekey_handle_via_command()` in `features/era_mousekey_via.c` | 1 cursor min, 2 cursor max, 3 cursor accel, 4 cursor interval, 5 wheel interval, 6 wheel accel |
| `ERA_VIA_DEBOUNCE_CHANNEL` 14 | `era_debounce_handle_via_command()` in `features/era_debounce_via.c` | 1 mode, 2 single time, 3 press time, 4 release time, 5 status |
| `ERA_VIA_TAPPING_CHANNEL` 15 | `era_tapping_handle_via_command()` in `features/era_tapping_via.c` | 1 legacy 1-byte/10 ms global term, 2 permissive hold, 3 hold-on-other-key-press, 4 retro tapping, 5 exact-ms global term |

**`13` was never allocated and is not a retired identifier.** Channels 10, 11,
12, 14 and 15 arrived with 13 already absent. The mouse page taking it recycles
nothing. Feature dispatch order in `era_common_via_handle_feature_command()`
(`system/era_common_via.c`): SOCD, KKUK, debounce, tapping, mousekey.

> **REFUSED:** renumber channels `9..15` to close the `6..8` gap.
> **WHY:** a 256-wide namespace with seven claimants has no scarcity to relieve;
> VIA caches a definition per (vendorId, productId), so an old definition
> against new firmware would write the wrong channels.
> **REOPENS:** a coordinated definition-and-firmware bump that accepts that
> cache miss.

## VIA Sync Value IDs

Same SYSTEM channel 9. `split/era_split_via_sync.h`:

| Id | Macro |
| ---: | --- |
| 5 | `ERA_SPLIT_VIA_SYNC_EEPROM_SYNC_REQUESTED_VALUE_ID` |
| 6 | `ERA_SPLIT_VIA_SYNC_INPUT_SYNC_REQUESTED_VALUE_ID` |
| 7 | `ERA_SPLIT_VIA_SYNC_RGB_SYNC_REQUESTED_VALUE_ID` |

Parent ids, the three `*_EFFECTIVE` ids, and the separate HOST-PEER/DUAL-HOST
EEPROM ids are retired and stay un-reused. Id 5 is the one relation-independent
EEPROM-sync request the storage lane consumes. Id 6 is live INPUT. Id 7 is the
DUAL-HOST RGB policy bit. Arms and gating: `era_authority_contract.md`
**Persisted Sync Policy**.

All three default ON (`split/era_split_sync_policy.h`): 5 and 6 since storage
version 4, 7 since version 5. **A requested id carries no route and no
execution authority.** That limit is not in the VIA header.

## VIA Split Link Value IDs

Same SYSTEM channel 9. `split/era_split_via_link.h`:

| Id | Macro |
| ---: | --- |
| 8 | `ERA_SPLIT_VIA_LINK_LEVEL_VALUE_ID` |
| 9 | `ERA_SPLIT_VIA_LINK_APPLY_VALUE_ID` |

Not a fourth sync family. Dropdown values are
`ERA_SPLIT_LINK_LEVEL_HIGH` 0, `MEDIUM` 1, `LOW` 2
(`storage/era_eeprom_layout.h`). The getter in `split/era_split_via_link.c`
reads the pending/stored level, not the running one. Id 9 is a
toggle-as-action and reads back 0, like the DFU and clean-confirm toggles on
this channel. USB re-enumerates only after the apply *commits*; an inert,
refused, or expired apply does not bounce. The MCU does not reset.

Ids 2, 3, 4 and 9 all raise an act on `split/era_split_restart_agreement.c`,
which holds one pending fact: a second act while one is in flight is refused.

> **REFUSED:** a read-only "running level" VIA control.
> **WHY:** VIA re-reads only on refresh, so it would answer an owner who already
> suspected something; after Reconciliation writes the stored level to the
> winner's, the dropdown already names what the wire runs.
> **REOPENS:** VIA grows a push that re-reads this page without a manual
> refresh.

## USB Session

Two USB frame-age thresholds exist. Only one resolves in a header.

- `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` 10 in `split/era_split_authority_reducer.h`:
  the window inside which the authority reducer treats the bus as live.
- **`ERA_USB_SESSION_SOF_STALE_MS` 300 is declared in `system/era_usb_session.c`
  and in no header.** Frame-loss arm of the sleep decision: frames absent that
  long on a port that has enumerated at least once since power-on mean the host
  is gone. It is the backstop for the controller's 3 ms suspend detector, not a
  race with it.

**No `WIRE_DIAG` field carries the frame sampler, the frame-loss arm, the sleep
decision, or its owner.** A sleep reading is by eye, or from the peer session
fields on `wire sess`. Unit ownership: `era_source_map.md`; ownership rule:
`era_authority_contract.md`.

**One local USB fact the auth line does carry: `wire auth … idle=a/b`.** `a` is
the host's last `SET_IDLE` duration in 4 ms units
(`tmk_core/protocol/usb_device_state.c`); `b`
is 1 while any IN report's idle rate is nonzero, computed as
`usb_idle_task()` in `tmk_core/protocol/chibios/usb_report_handling.c`
computes it. `idle=0/0` means the host asked for report-on-change only. On this
image that task is paced to once per millisecond (`era_qmk_fork_ledger.md`).

**There is no field for the INPUT or RGB requested policy bits.** `eeprom pol
req=` is the EEPROM bit alone. `df=` is a change log, not a value. **`sync` on
`wire sess` is not the local policy view.** A sitting that needs the three bits
reads them back in VIA. From storage version 5 all three default on
(`era_authority_contract.md`).
