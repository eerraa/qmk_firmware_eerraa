# ERA Identifier Map

Genre: map
Canonical for: the identifier space no header owns — VIA keyboard-channel
bands, GET_KEYBOARD_VALUE selector `0x06`, feature-channel allocation,
modes/routes extra facts, storage service causes, retired payload and status
numbers, VIA sync and split-link value-id facts, USB-session thresholds —
and every retired number that stays reserved un-reused

**A retired identifier's number is never recycled.** Captured console lines are
dated by what their numbers meant when they were taken, so a re-use would make
an old reading silently wrong instead of merely historical. Every "retired,
stays allocated" statement below is that one rule applied.

**Decoding a console line is `era_capture_reading.md`'s**, and the split is by
what the name denotes: an identifier here names a thing in the source, a field
there names a column in a `WIRE_DIAG` capture. The rule above serves both, and
it is why a capture from an older image still decodes against this file.

## Modes

`era_split_mode_planner.h` names five values: `LOCAL_NO_LINK` 0, the two
HOST-PEER roles 1–2, the two DUAL-HOST roles 3–4. Values 5 and 6 have no
enumerator (were `HOST_HOST_LEFT`/`HOST_HOST_RIGHT`) and stay un-reused.

## Routes And Route Reasons

`era_split_wire_router.h` names both enums, each with `_NONE = 0`. Every live
value has a producer. The unproduced `RUNTIME_PUSH` kind and the
`HOST_SOURCE_RESPONSE_POLL` and `HOST_PEER_LIVENESS` reasons went with the
captures that needed them. What each live reason means is canonical in
`era_route_contract.md`.

**Read `rr`, not `rk`, to tell two runtime routes apart.**
`ERA_SPLIT_ROUTE_HOST_PEER_HEARTBEAT` is live and keeps its value. Core1's
standing service stamps that kind into the transaction timing for both live
runtime reasons — a HOST-PEER capture reads the heartbeat kind for traffic
that is not the heartbeat route. `rk` alone does not distinguish a
core0-selected route from a granted exchange, and in a serviced relation it is
always the latter.

## Storage Service Causes

`HOST_PEER_STORAGE`, `HOST_PEER_STORAGE_IDLE_PROOF`, and
`HOST_PEER_STORAGE_ACTIVE` are contract-level service causes only. Storage is
a dedicated cold-task lane, not an owner route kind
(`era_route_contract.md`), so **no C enumerator of any of those names exists**,
and the probes `IDLE_PROOF` describes are not a periodic patrol. **Do not add
them to `era_split_wire_router.h`**.

**Every probe is queued from a comparison of both halves' facts**: the summary
cell — the mandatory relation-open audit sweep and every later `SUMMARY_STATUS`
result — and the conflict cell when this half loses. Those are the only two
writers of `probe_pending_mask`. A peer advertisement reaches
`era_host_peer_storage_note_host_news()` in `split/era_host_peer_storage.c`,
which arms a summary refresh and touches that mask nowhere. Mechanism:
`era_host_peer_storage_contract.md`.

## Payloads

`era_split_wire_protocol.h` names the payload classes, HOST-PEER op ids,
HSRSP section mask bits, visual-resync reasons, and RGB/mask/time-anchor field
widths. `era_split_eeprom_sync.h` names storage ops `0xE0`..`0xEE` (`0xEF`
reserved), push phases, status ids, and the seven portable domains.

**Two payload kind values are retired and stay allocated un-reused**, so the
`rsp` kind numbering in a captured log keeps its meaning: `6` was `DUAL_HOST`
and `4` was `ERROR_NACK` (class `0x40`). Every op byte the split layer writes
is `0x10`, `0x20`, `0x21` or `0xE0..0xEF`, and the `0x40` class falls to the
`default` reject.

Storage status ids, in order from zero, in `era_split_eeprom_sync.h`: MATCH,
TRANSFER, APPLY_READY, COMPLETE, ABORTED, POLICY_CLOSED, UNSUPPORTED_DOMAIN,
UNSUPPORTED_SCHEMA, SIZE_MISMATCH, STALE, BUSY, INTEGRITY_FAIL, RESULT_FULL,
TIMEOUT, ROLE_CHANGED, SOURCE_CHANGED. Exact payload shapes and response
meaning are canonical in `era_host_peer_storage_contract.md`. **`RESULT_FULL`
has no producer and is kept anyway**: it is a wire status id in a numbered
sequence, so deleting it would renumber the three live values after it.

Control-only semantic names, not class/op identifiers:

- `HOST_PEER_HEARTBEAT`
- `HOST_PEER_ACK_STATUS`

## VIA Keyboard-Channel Value IDs

Channel `id_custom_channel` (0) carries several claimants. The split is by
band, not by owner: `era_common_via_handle_keyboard_channel_command()` in
`system/era_common_via.c` runs the common ones before the board hook in
`system/era_board_hooks.c`.

| Band | Owner |
| --- | --- |
| `0..3` | PWM backlight effect layer — brightness, effect, breathing period, blink speed (`features/era_backlight_via.h`), only where `ERA_BACKLIGHT_EFFECT_ENABLE` is on |
| `0..4` | otherwise a board's own, through the weak `era_board_via_get_value`/`_set_value` pair: tomak family `0..4` (lock indicator, override, brightness, colour, badge-only) in `sirind/common/tomak_common.h`; odessey `1..4` (indicator select, brightness, colour, Velocikey; id `0` unused) in `newone/common/odessey_common.h` |
| `5` | NKRO toggle. `ERA_VIA_NKRO_ENABLE_VALUE_ID` is 5 (`features/era_nkro_via.h`). Twenty-five RP2040 VIA JSON files address `["id_qmk_custom_nkro_enable", 0, 5]`. `sirind/brick65` has no FEATURE menu |
| `6..12` | RGB Matrix lock-indicator slots — master enable, then source, brightness, colour per slot (`features/era_rgb_indicator_via.h`), only where `ERA_RGB_INDICATOR_ENABLE` is on. A one-slot board answers `6..9` and declines `10..12` |
| `32..71` | eight tap-dance slots, five ids each (tap, hold, double-tap, tap-hold, legacy term) (`features/era_tapdance_via.c`). Legacy term field ids are `36,41,46,51,56,61,66,71` |
| `72..79` | exact-ms tap-dance terms for TD0–TD7, additive, 2-byte big-endian milliseconds (`features/era_tapdance_via.c`) |

**The `0..3` overlap is real and is held apart by the selector, not by the
numbers.** The backlight ids are what the shipped definitions of the backlight
boards address. No board has both a PWM backlight and a keyboard-channel
handler of its own. This common router runs ahead of the board hook, so a board
that grew both would have to give one of them up.

**`6..12` overlaps nothing.** The indicator feature was drafted to extend
odessey's band with a second slot that would have put a brightness slider on
`5` — the NKRO toggle, answered before the board hook. The ids were free to
start above the contested band. **A new claimant on this channel checks the
whole table above, not the band it was aiming at.**

`72..79` does not reuse the legacy term field ids. Firmware in
`features/era_tapdance_via.c` answers both bands; both stay. Tree JSON in
each board's `keymaps/via` folder is the stock-VIA/legacy surface; the VIA
app (`the-via-eerraa`) presents exact-ms. That split is intentional dual
compatibility with usevia.app, not a mismatch to resolve. Channel 15 value
id `5` is the matching exact global tapping term
(`features/era_tapping_via.c`); ids `1..4` on that channel stay the legacy
1-byte/10 ms global term and the three booleans.

## VIA GET_KEYBOARD_VALUE selectors

Command `id_get_keyboard_value` `0x02` in `quantum/via.h`. VIA uses `0x01..0x04`
on GET and `0x05` on SET. ERA's polling-first revision query is selector
**`0x06`** (`ERA_STATE_SYNC_KEYBOARD_VALUE` in `system/era_state_sync.h`;
handler `era_state_sync_via_command()` in `system/era_state_sync.c`, reached
from `via_command_kb()` in `system/era_via_system.c`). It is not a
custom-channel value id.

## VIA Feature Channels

VIA itself reserves `0..5` (`quantum/via.h`). ERA uses `9..15`. `6..8` are
idle. **The channels are not renumbered to close that gap**: VIA caches a
definition per (vendorId, productId), so an old definition against new firmware
would write to the wrong channels.

| Channel | Owner |
| --- | --- |
| `9` | SYSTEM (`system/era_via_system.h`). Split routing in `split/era_split_keyboard.c`: system first, then sync, then link |
| `10` | SOCD left/right (`storage/era_eeprom_layout.h`, `features/era_socd_via.c`) |
| `11` | SOCD up/down (same unit, other pair) |
| `12` | KKUK (`features/era_kkuk_via.c`) |
| `13` | mousekey (`features/era_mousekey_via.c`) |
| `14` | debounce (`features/era_debounce_via.c`) |
| `15` | tapping (`features/era_tapping_via.c`) |

SYSTEM value ids: DFU, three EEPROM-clean confirms, EEPROM/INPUT/RGB requested, link level, link Apply — in that order, starting at one (`system/era_via_system.h`, `split/era_split_via_sync.h`, `split/era_split_via_link.h`). SOCD (both channels) and KKUK each use enable, then two data fields, then mode, starting at one. Mousekey uses six cursor/wheel fields starting at one. Debounce uses mode then three delays starting at one. Tapping uses the legacy global term, three booleans, then the exact global term, starting at one.

**`13` was never allocated when `10`, `11`, `12`, `14` and `15` arrived**, and
is not a retired identifier. The mouse page taking it recycles nothing. H7S
VIA definitions use a different mouse channel and put USB polling on thirteen;
that is the other firmware family, not this tree.

## VIA Sync Value IDs

`era_split_via_sync.h` names value ids 5..7 on the SYSTEM channel. The parent
ids, the three child `*_EFFECTIVE` ids and the separate HOST-PEER/DUAL-HOST
EEPROM ids are retired and their numbers stay un-reused.
`ERA_SPLIT_VIA_SYNC_EEPROM_SYNC_REQUESTED_VALUE_ID` (5) is the one
relation-independent request the storage lane consumes. INPUT (6) is live.
RGB (7) is the DUAL-HOST RGB policy bit. Arms, gating, and HOST-PEER reach:
`era_authority_contract.md` **Persisted Sync Policy**.

**All three default ON** (`ERA_SPLIT_SYNC_POLICY_DEFAULT_FLAGS` in
`split/era_split_sync_policy.c`). Storage version is
`ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION` 5 in `split/era_split_sync_storage.h`.
A capture that predates version 5 was taken with RGB off unless someone set
it, and there is no console field that records which — see **USB Session**.

**A requested id carries no route and no execution authority.** The header
states no such limit, so this line is the only statement of it.

## VIA Split Link Value IDs

`era_split_via_link.h` names value ids 8 and 9 on the same SYSTEM channel: the
level dropdown and the Apply toggle. They are **not** a fourth sync family — a
sync policy is an owner preference each half may hold differently, while the
wire must run one level or no session opens (`era_authority_contract.md`).

The dropdown's three values are `ERA_SPLIT_LINK_LEVEL_HIGH/MEDIUM/LOW` at
`0/1/2` in `storage/era_eeprom_layout.h`, High asserted at zero. GET of id 8
returns `era_split_link_pending_level()` in `split/era_split_link.c`, which
seeds from the *stored* level. A Right half still walking the ring toward its
talker therefore shows the owner what they chose, not the boot-Low running
rate.

**9 is a toggle-as-action and reads back 0**, like the DFU and clean-confirm
toggles on this channel. USB re-enumerates only after that request *commits*
(`split/era_split_via_link.c`); an inert, refused, or expired apply does not
bounce, so Enable staying on is how the owner sees that nothing ran. The MCU
does not reset.

**Ids 2, 3, 4 and 9 all reach the same mechanism.** The three clean-confirm
toggles and the link Apply both raise an act on
`split/era_split_restart_agreement.c`, which holds **one** pending fact: a
second act raised while one is in flight is refused.

## USB Session

Two USB frame-age thresholds exist, they answer different questions, and only
one of them resolves in a header.

- `ERA_SPLIT_AUTHORITY_SOF_FRESH_MS` (10) is declared in
  `era_split_authority_reducer.h`: the window inside which the authority
  reducer treats the bus as live.
- **`ERA_USB_SESSION_SOF_STALE_MS` (300) is defined in `era_usb_session.c`
  and in no header.** It is the frame-loss arm of the sleep decision: frames
  absent for that long on a port that has enumerated at least once since
  power-on mean the host is gone. The value is long rather than short
  deliberately — this arm is the backstop for the controller's own 3 ms
  suspend detector, not a race with it. `era_usb_session.h` names the fact
  and does not define the number.

**No `WIRE_DIAG` field carries the frame sampler, the frame-loss arm, the sleep
decision or its owner.** A sleep reading is by eye, or from the peer session
fields on `wire sess`. Unit ownership is canonical in `era_source_map.md`, the
ownership rule in `era_authority_contract.md`.

**One local USB fact the auth line does carry: `wire auth … idle=a/b`.** `a` is
the host's last `SET_IDLE` duration in 4 ms units (`usb_device_state.c`), `b`
is 1 while any IN report's idle rate is nonzero — computed the way
`usb_idle_task()` (`tmk_core/protocol/chibios/usb_report_handling.c`) computes
it. `idle=0/0` means the host asked for report-on-change only. On the ERA image
the task's walk is paced to once per millisecond (`era_qmk_fork_ledger.md`).

**There is likewise no field for the INPUT or RGB requested policy bits.**
`eeprom pol req=` is the EEPROM bit alone. `df=` is a change log, not a value.
**`sync` on `wire sess` is not the local policy view and no such field exists
in any profile.** From storage version 5 all three default on, so the check is
"has anyone turned one off" (`era_authority_contract.md`).
