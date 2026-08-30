# ERA HID Keyboard Report Contract

Genre: contract
Canonical for: the HID keyboard report ERA sends the host — its shape and
width, the boot-protocol fallback that keeps BIOS working, the opt-in NKRO
policy, the refusals that keep report-array width and endpoint size tied to
the upstream values, and the rule that any code that empties and restores
the report buffer must follow the active format

VIA value id 5: `era_identifier_map.md`. Core-file fork surface:
`era_qmk_fork_ledger.md`. HOST-PEER carries matrix rows, not this report:
`era_host_peer_matrix_contract.md`.

Owner decision: keep A — a spec 6-key report plus an opt-in NKRO toggle,
both default off. The KKUK pulse is independent of that choice.

## Report shape

`report_keyboard_t` in `tmk_core/protocol/report.h` is mods + reserved +
`keys[KEYBOARD_REPORT_KEYS]`. `KEYBOARD_REPORT_KEYS` is 6.
`KEYBOARD_REPORT_SIZE` is 8 unless `KEYBOARD_SHARED_EP`, which no ERA file
defines. `report_id` exists only under that define. The only ERA
`SHARED_EP_ENABLE` test is the idle-rate walk in
`split/diagnostics/era_split_wire_diagnostics.c`.

| Fact | Value | Where |
| --- | --- | --- |
| Keyboard interface | own interface and endpoint; subclass `HID_CSCP_BootSubclass`, protocol `HID_CSCP_KeyboardBootProtocol` | `tmk_core/protocol/usb_descriptor.c` |
| `KEYBOARD_EPSIZE` | 8; endpoint descriptor uses that value | `tmk_core/protocol/usb_descriptor.h`, `tmk_core/protocol/usb_descriptor.c` |
| Poll | `USB_POLLING_INTERVAL_MS` default 1. ERA does not override. RP2040 is full-speed; this tree has no 8000 Hz send path | `tmk_core/protocol/usb_descriptor.c` |
| HID report | `KeyboardReport[]`; keycode array `REPORT_COUNT` 6 × `REPORT_SIZE` 8 | `tmk_core/protocol/usb_descriptor.c` |

`tmk_core/protocol/report.h` is not on the fork surface in
`era_qmk_fork_ledger.md`. `tmk_core/protocol/usb_descriptor.c` and
`tmk_core/protocol/usb_descriptor.h` are, for identity override hooks, not
for width.

## Boot and report protocol

| Event | State | Where |
| --- | --- | --- |
| Power-on | `USB_PROTOCOL_REPORT` | `tmk_core/protocol/usb_device_state.c` |
| USB reset | `USB_PROTOCOL_REPORT` | `tmk_core/protocol/chibios/usb_main.c` |
| GET_PROTOCOL | answers only when `wIndex == KEYBOARD_INTERFACE` | `tmk_core/protocol/chibios/usb_main.c` |
| SET_PROTOCOL | writes protocol only when `wIndex == KEYBOARD_INTERFACE`; always returns a 0-length transfer | `tmk_core/protocol/chibios/usb_main.c` |
| No SET_PROTOCOL | stays `USB_PROTOCOL_REPORT` | — |

`send_keyboard()` in `tmk_core/protocol/chibios/usb_main.c` sends 8 bytes
from `&report->mods` under `USB_PROTOCOL_BOOT`, else `KEYBOARD_REPORT_SIZE`.
Without `KEYBOARD_SHARED_EP` those arms both send 8 bytes. The boot cut is
HID 1.11 §7.2.6; it becomes a length change if the report grows or gains a
report id.

`usb_get_report_cb()` in `tmk_core/protocol/chibios/usb_report_handling.c`
returns the last queued report by interface→endpoint lookup.
`usb_endpoint_in_send()` in `tmk_core/protocol/chibios/usb_driver.c` stores
the bytes actually queued, so GET_REPORT answers 8 bytes in boot.

> **REFUSED:** delete the boot branch of `send_keyboard()` and always send the extended report.
> **WHY:** that branch is the HID 1.11 §7.2.6 compliance this firmware already ships; deleting it saves a few lines and loses correctness on every host that actually negotiates boot protocol.
> **REOPENS:** evidence that variable-length send has a measurable cost — on this tree's 1 ms poll and queue-based send, length is only an argument, so that evidence is not here.

## NKRO

`report_nkro_t` in `tmk_core/protocol/report.h` is report_id + mods +
`bits[NKRO_REPORT_BITS]`. `NKRO_REPORT_BITS` is 30 → 32 bytes, 240 keys.
`NKRO_ENABLE` sets `SHARED_EP_ENABLE` in `tmk_core/protocol.mk`.
`send_nkro()` in `tmk_core/protocol/chibios/usb_main.c` sends
`USB_ENDPOINT_IN_SHARED`. That endpoint is `SHARED_EPSIZE` 32 in
`tmk_core/protocol/usb_descriptor.h`, with storage entry `REPORT_ID_NKRO` in
`tmk_core/protocol/chibios/usb_endpoints.c`. While NKRO is active,
`send_keyboard_report()` in `quantum/action_util.c` returns after
`send_nkro_report()` and does not update the boot keyboard interface.

The live branch is `host_can_send_nkro() && keymap_config.nkro` in
`send_keyboard_report()` (`quantum/action_util.c`) and the same AND in
`tmk_core/protocol/report.c` (`has_anykey()`, `get_first_key()`,
`is_key_pressed()`, `add_key_to_report()`, `del_key_from_report()`,
`clear_keys_from_report()`). `host_can_send_nkro()` in
`tmk_core/protocol/host.c` is USB protocol == `USB_PROTOCOL_REPORT`.
Automatic 6KRO in BIOS is exactly SET_PROTOCOL(boot).

`NKRO_DEFAULT_ON` is false when undefined (`quantum/eeconfig.c`). ERA
defines neither it nor `FORCE_NKRO` (`quantum/keyboard.c`).
`keymap_config.nkro` exists regardless of `NKRO_ENABLE`
(`quantum/keycode_config.h`); `sizeof(keymap_config_t)` is 2. Dropping NKRO
from the build does not change the EEPROM layout.

On a host that never sends SET_PROTOCOL the state stays
`USB_PROTOCOL_REPORT`, so `host_can_send_nkro()` (`tmk_core/protocol/host.c`)
is true, and if `keymap_config.nkro` is on every key leaves only on the
shared endpoint. A host that opened only the boot keyboard interface then
sees **no input at all**. Which BIOS/KVM do this is an owed measurement;
`user/readme.txt` already tells the owner to turn NKRO off when an old BIOS
or boot menu cannot read the keyboard.

> **REFUSED:** ship NKRO on by default via `NKRO_DEFAULT_ON`.
> **WHY:** on a host that never sends SET_PROTOCOL nothing leaves the keyboard interface, so input disappears, and the gain is a default that applies only after `eeconfig_init_quantum()` (`quantum/eeconfig.c`) — boards that already stored eeconfig stay 6KRO.
> **REOPENS:** SET_PROTOCOL(boot) is observed on the BIOS/KVM sample the owner treats as the target.

## Boards and VIA

23 boards with a `keyboard.json` all have `features.nkro` true. The VIA
toggle compiles on 22: under `VIA_ENABLE`, `NKRO_ENABLE = yes` adds
`features/era_nkro_via.c` and `-DERA_NKRO_VIA_ENABLE`
(`system/era_common_qmk_rules.mk`). `sirind/brick65/post_rules.mk` includes
only build-name and option-print rules — the atmega32u4 permanent exception
— and `sirind/brick65/keymaps/via/BRICK65-VIA.json` has no FEATURE menu. 26 `*-VIA.json` files (three
splits ship L/R); 25 carry the NKRO menu. Value id
`ERA_VIA_NKRO_ENABLE_VALUE_ID` 5 (`features/era_nkro_via.h`) on
`id_custom_channel` (0). Every one of those 25 files addresses it as
`["id_qmk_custom_nkro_enable", 0, 5]`.

`features/era_nkro_via.c` is an adapter: it owns no state and writes QMK's
`keymap_config.nkro` and eeconfig. What it owns is the two
`clear_keyboard()` calls around the switch, so a key held across the format
change is not reported in one format and released in the other. There is no
`id_custom_save` arm.

The same 25 files label the FEATURE submenu exactly `KKUK`.
The Mode dropdown's only option is `Report Pulse`, value
`ERA_KKUK_MODE_REPORT_PULSE` 1 (`features/era_kkuk.h`). The page is
`ERA_VIA_KKUK_CHANNEL` 12 (`storage/era_eeprom_layout.h`); value ids
Enable/Delay/Repeat/Mode are 1/2/3/4 in `features/era_kkuk_via.c`.

## KKUK pulse

`era_kkuk_start_empty_pulse()` in `features/era_kkuk.c` sends an empty
report with every held key released; the next task tick's
`era_kkuk_finish_restore_pulse()` in that file calls
`send_keyboard_report()` (`quantum/action_util.c`) to restore. The snapshot
must be the report `clear_keys()` will empty, and that is not always 6KRO:
`clear_keys_from_report()` in `tmk_core/protocol/report.c` writes
`nkro_report->bits` while NKRO is negotiated, else `keyboard_report->keys`.
The pulse saves both arrays and restores both; the uncleared side writes
its own bytes back and changes nothing, so this file does not copy QMK's
predicate. Both snapshots are function-local; only `kkuk_pulse_state`
survives the tick. Keys only: `clear_keys()` in `quantum/action_util.h`
does not clear mods, and each send recomputes them via
`get_mods_for_report()` in `quantum/action_util.c`.

> **REFUSED:** the pulse snapshots and restores `keyboard_report` only.
> **WHY:** under NKRO `clear_keys()` empties `nkro_report->bits`, so there is nothing to restore, and `send_nkro_report()` (`quantum/action_util.c`) memcmp duplicate-suppression swallows the restore as equal to the empty report just sent — held keys stay released on the host until pressed again.
> **REOPENS:** this firmware has only one report format. Builds without `NKRO_ENABLE` already fold to that shape behind `#ifdef`.

Neither feature ships on. `era_kkuk_apply_defaults()` in `features/era_kkuk.c`
leaves `enable` 0. NKRO default is above. User docs introduce them as
independent items (`user/readme.txt`, `user/readme_split.txt`), which is
true while this contract holds.

## Split and storage

The report never crosses the wire. The only ERA site that touches the HID
report buffers is the KKUK pulse; the only other ERA
`send_keyboard_report()` call is one retransmission on HID re-open in
`split/era_split_authority_reducer.c`. The NKRO bit already syncs: QMK
`keymap_config` is the 2-byte domain
`ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES` 2
(`split/era_host_peer_storage.h`). The three split boards (`sirind/tomak`,
`sirind/tomak79h`, `sirind/tomak79s`) set
`ERA_SPLIT_EEPROM_SYNC_ENABLE := yes` on the VIA build. Changing report
width does not change schema or domain size.

## Width

`KEYBOARD_EPSIZE` 8 cannot carry a 22-byte report:
`usb_endpoint_in_send()` in `tmk_core/protocol/chibios/usb_driver.c`
refuses `size > endpoint->config.buffer_size`. A wider 6KRO array therefore
forces `KEYBOARD_EPSIZE` to 32, and that value is the keyboard endpoint
descriptor in `tmk_core/protocol/usb_descriptor.c`. NKRO already rides
`SHARED_EPSIZE` 32; widening 6KRO is a keyboard-endpoint change, not a
shared-endpoint one. The reachable ceiling is already 240 behind the NKRO
toggle. What a wider array would buy is a different default for a user who
never opens VIA.

> **REFUSED:** widen the keyboard array and give up the boot-protocol truncation.
> **WHY:** this tree already implements the truncation, and deleting it is not a negative-cost change, so the only reason left to create a new spec deviation is that another project did.
> **REOPENS:** device evidence that the truncation branch itself causes a measured defect (for example report loss on a protocol-switch boundary).

D′ (widen, keep the boot cut, keep the NKRO toggle) is the only technically
coherent upgrade and is strictly better than D: it loses no ceiling and
does not touch 25 VIA files or the user docs. Still refused — the gain is
default 6→20 against a permanent `report.h` fork and a 22-board descriptor
change. D/D′ also do not remove the no-SET_PROTOCOL risk; they change the
failure mode from no input to babble.

> **REFUSED:** grow `KEYBOARD_REPORT_KEYS` and `KEYBOARD_EPSIZE` for ERA and detach the keyboard report width from upstream.
> **WHY:** the gain is one default (6 → 20) against a ceiling that is already 240, and the cost is putting upstream-identical `tmk_core/protocol/report.h` on the permanent fork surface plus changing the USB descriptors of 22 shipped boards.
> **REOPENS:** the owner promotes "default rollover for a user who never opens VIA" to a product requirement, and the two owed measurements (SET_PROTOCOL sample, extended-report boot compatibility) pass. The choice then is D′, not D.

## Owed measurements

Source cannot answer these. 1–2 are the entry conditions for reopening D′;
3 is live now.

| # | Question | How |
| --- | --- | --- |
| 1 | Does the owner's target BIOS/KVM sample send SET_PROTOCOL(boot)? | Enter a boot menu with NKRO on; input alive means that host sent SET_PROTOCOL. A USB analyzer is better |
| 2 | How far do hosts accept a 22-byte report on a boot-subclass interface? | Sister-project field evidence does not transfer. Re-test on the same sample as 1 — those are exactly the hosts that receive the extended report |
| 3 | After a descriptor change without a `usb.device_version` bump, does the host keep a cached parse? | Needs a reproduction on one shipped board |

Device falsifier for the pulse: NKRO on + KKUK on, hold two or more keys;
the host must lose none.
