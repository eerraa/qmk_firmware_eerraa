# ERA HID Keyboard Report Contract

Genre: contract
Canonical for: the HID keyboard report ERA sends the host — its shape and
width, the boot-protocol fallback that keeps BIOS working, the opt-in NKRO
policy, the refusals that keep report-array width and endpoint size tied to
the upstream values, and the rule that any code that empties and restores
the report buffer must follow the active format

## Decision

ERA keeps a spec-compliant 6-key report plus an opt-in NKRO toggle. Widening
the array would buy only a different default; NKRO is already here and larger
than 20 keys. The KKUK pulse that failed to restore held keys under NKRO is
independent of that choice and is a contract below.

## Report Shape

| Fact | Value |
| --- | --- |
| `report_keyboard_t` | mods + reserved + `keys[KEYBOARD_REPORT_KEYS]`; `KEYBOARD_REPORT_KEYS` 6 (`tmk_core/protocol/report.h:145`) |
| Width | 8 bytes, no report id. `report_id` exists only under `KEYBOARD_SHARED_EP`, which no ERA file defines. The only ERA `SHARED_EP_ENABLE` test is `split/diagnostics/era_split_wire_diagnostics.c:815` |
| Interface | own interface and endpoint; subclass `HID_CSCP_BootSubclass`, protocol `HID_CSCP_KeyboardBootProtocol` (`tmk_core/protocol/usb_descriptor.c:572`) |
| `KEYBOARD_EPSIZE` | 8 (`tmk_core/protocol/usb_descriptor.h:287`); endpoint descriptor at `tmk_core/protocol/usb_descriptor.c:594` |
| Poll | 1 ms (`USB_POLLING_INTERVAL_MS` default, `tmk_core/protocol/usb_descriptor.c:539`); ERA does not override. RP2040 is full-speed; this tree has no 8000 Hz send path |
| Descriptor | `KeyboardReport[]` (`tmk_core/protocol/usb_descriptor.c:71`); keycode array `REPORT_COUNT` 6 × `REPORT_SIZE` 8 (`:98`) |

## Boot And Report Protocol

| Event | State |
| --- | --- |
| Power-on | `USB_PROTOCOL_REPORT` (`tmk_core/protocol/usb_device_state.c:27`) |
| USB reset | `USB_PROTOCOL_REPORT` (`tmk_core/protocol/chibios/usb_main.c:159`) |
| SET_PROTOCOL / GET_PROTOCOL | change or answer only when `wIndex == KEYBOARD_INTERFACE` (`tmk_core/protocol/chibios/usb_main.c:285`, `:260`); otherwise SET answers 0-length and leaves the state |
| No SET_PROTOCOL | stays `USB_PROTOCOL_REPORT` |

`send_keyboard()` (`tmk_core/protocol/chibios/usb_main.c:437`) sends 8 bytes from `&report->mods` under boot protocol, else `KEYBOARD_REPORT_SIZE`. `usb_get_report_cb()` (`tmk_core/protocol/chibios/usb_report_handling.c:81`) returns the last sent report by interface→endpoint lookup; stored length is the bytes actually queued (`tmk_core/protocol/chibios/usb_driver.c:205`), so GET_REPORT answers 8 bytes in boot.

> **REFUSED:** delete the boot branch of `send_keyboard()` and always send the extended report (a direct port of the sister project's path).
> **WHY:** that branch is the HID 1.11 §7.2.6 compliance this firmware already ships; deleting it saves a few lines and loses correctness on every host that actually negotiates boot protocol.
> **REOPENS:** evidence that variable-length send has a measurable cost — on this tree's 1 ms poll and queue-based send, length is only an argument, so that evidence is not here.

## NKRO Path

| Fact | Value |
| --- | --- |
| `report_nkro_t` | report_id + mods + `bits[NKRO_REPORT_BITS]`; `NKRO_REPORT_BITS` 30 → 32 bytes, 240 keys (`tmk_core/protocol/report.h:137`) |
| Endpoint | shared; `send_nkro()` sends `USB_ENDPOINT_IN_SHARED` (`tmk_core/protocol/chibios/usb_main.c:446`); storage entry `REPORT_ID_NKRO` (`tmk_core/protocol/chibios/usb_endpoints.c:38`). While NKRO is active, interface 0 sends nothing |
| Branch | `host_can_send_nkro() && keymap_config.nkro` in `send_keyboard_report()` (`quantum/action_util.c:335`) and the same AND in `tmk_core/protocol/report.c` (`has_anykey()`, `is_key_pressed()`, `add_key_to_report()`, `del_key_from_report()`, `clear_keys_from_report()`) |
| `host_can_send_nkro()` | USB path is protocol == `USB_PROTOCOL_REPORT` (`tmk_core/protocol/host.c:137`). Automatic 6KRO in BIOS is exactly SET_PROTOCOL(boot) |
| Defaults | `NKRO_DEFAULT_ON` is false when undefined (`quantum/eeconfig.c:50`); ERA defines neither it nor `FORCE_NKRO` (`quantum/keyboard.c:513`) |
| EEPROM | `keymap_config.nkro` exists regardless of `NKRO_ENABLE` (`quantum/keycode_config.h:39`); `sizeof(keymap_config_t)` is 2 (`:48`). Dropping NKRO from the build does not change the EEPROM layout |

## Hosts That Never Send SET_PROTOCOL

On those hosts the state stays `USB_PROTOCOL_REPORT`, so `host_can_send_nkro()` (`tmk_core/protocol/host.c`) is true, and if `keymap_config.nkro` is on every key leaves only on the shared endpoint. A host that opened only the boot keyboard interface then sees **no input at all**. Which BIOS/KVM do this is an owed measurement; `user/readme.txt` already tells the owner to turn NKRO off when an old BIOS or boot menu cannot read the keyboard.

## Boards And VIA

23 boards with a `keyboard.json` all have `features.nkro` true. The VIA toggle compiles on 22: `NKRO_ENABLE = yes` adds `features/era_nkro_via.c` and `-DERA_NKRO_VIA_ENABLE` (`system/era_common_qmk_rules.mk:257`). `sirind/brick65/post_rules.mk` includes only build-name and option-print rules — the atmega32u4 permanent exception — and its VIA definition has no NKRO menu. 26 `*-VIA.json` files (three splits ship L/R); 25 carry the NKRO menu. Value id `ERA_VIA_NKRO_ENABLE_VALUE_ID` 5 (`features/era_nkro_via.h:17`) on `id_custom_channel`.

`features/era_nkro_via.c` is an adapter: it owns no state and writes QMK's `keymap_config.nkro` and eeconfig. What it owns is the two `clear_keyboard()` calls around the switch, so a key held across the format change is not reported in one format and released in the other. There is no `id_custom_save` arm.

## Core Fork Practice

Every core fork owes one ledger line; the derivation and the current surface are `era_qmk_fork_ledger.md`. `tmk_core/protocol/report.h` is identical to upstream (not in that diff), which is what grounds the width refusal below.

## KKUK Pulse

`era_kkuk_start_empty_pulse()` in `features/era_kkuk.c` sends an empty report with every held key released; the next task tick's `era_kkuk_finish_restore_pulse()` calls `send_keyboard_report()` to restore. The snapshot must be the report `clear_keys()` will empty, and that is not always 6KRO: `clear_keys_from_report()` (`tmk_core/protocol/report.c`) writes `nkro_report->bits` while NKRO is negotiated, else `keyboard_report->keys`. The pulse saves both arrays and restores both (`features/era_kkuk.c:163-176`); the uncleared side writes its own bytes back and changes nothing, so this file does not copy QMK's predicate. Both snapshots are function-local; only `kkuk_pulse_state` survives the tick. Keys only: `clear_keys()` (`quantum/action_util.h`) does not clear mods, and each send recomputes them via `get_mods_for_report()` (`quantum/action_util.c`).

> **REFUSED:** the pulse snapshots and restores `keyboard_report` only.
> **WHY:** under NKRO `clear_keys()` empties `nkro_report->bits`, so there is nothing to restore, and `send_nkro_report()` (`quantum/action_util.c`) memcmp duplicate-suppression swallows the restore as equal to the empty report just sent — held keys stay released on the host until pressed again.
> **REOPENS:** this firmware has only one report format. Builds without `NKRO_ENABLE` already fold to that shape behind `#ifdef`.

Neither feature ships on. KKUK `enable = 0` via `era_kkuk_apply_defaults()` (`features/era_kkuk.c`); NKRO default is above. User docs introduce them as independent items (`user/readme.txt`, `user/readme_split.txt`), which is true while this contract holds.

## Split And Storage

The report never crosses the wire. HOST-PEER carries the raw matrix, not a HID report (`era_host_peer_matrix_contract.md`). The only ERA site that touches the HID report buffers is the KKUK pulse; the only other ERA `send_keyboard_report()` call is one retransmission on HID re-open in `split/era_split_authority_reducer.c`. The NKRO bit already syncs: QMK `keymap_config` is the 2-byte domain `ERA_HOST_PEER_STORAGE_DOMAIN_QMK_KEYMAP_CONFIG_BYTES` 2 (`split/era_host_peer_storage.h:53`). The three split boards (`sirind/tomak`, `sirind/tomak79h`, `sirind/tomak79s`) set `ERA_SPLIT_EEPROM_SYNC_ENABLE := yes` on the VIA build. Changing report width does not change schema or domain size.

## Wider Array Forces A Descriptor Change

`KEYBOARD_EPSIZE` 8 cannot carry a 22-byte report: `usb_endpoint_in_send()` refuses `size > endpoint->config.buffer_size` (`tmk_core/protocol/chibios/usb_driver.c:253`). A wider array therefore forces `KEYBOARD_EPSIZE` to 32, and that value is the endpoint descriptor (`tmk_core/protocol/usb_descriptor.c:594`).

## Keep Option A

Owner decision: keep A (6-key + opt-in NKRO, default off).

1. **No functional gain.** The sister project's 20-key array was a substitute for NKRO. Here NKRO is already 240 keys behind one toggle. C/D drop the reachable ceiling from 240 to 20. What remains is whether a user who never opens VIA defaults to 6 or 20.
2. **The problem is not in this tree.** The sister kept two deviations because they were already shipping and changing them meant making an 8 kHz path variable-length. This tree has neither deviation nor an 8 kHz path.
3. **The cost is permanent.** `tmk_core/protocol/report.h` is identical to upstream; every off-layer keyboard in this fork compiles it. An ungated width change changes all of their report structs and owes the ledger's off-layer build confirmation. A gated change is safer and still puts `report.h` on the fork surface forever.
4. **The one-shot cost is 22 shipped boards.** A descriptor change can disagree with a host-cached parse; the lever is `usb.device_version` on each of 22 `keyboard.json` files. Removing the toggle also moves 25 VIA definition files and both user docs.

> **REFUSED:** ship NKRO on by default via `NKRO_DEFAULT_ON`.
> **WHY:** on a host that never sends SET_PROTOCOL nothing leaves the keyboard interface, so input disappears, and the gain is a default that applies only after `eeconfig_init_quantum()` (`quantum/eeconfig.c:89`) — boards that already stored eeconfig stay 6KRO.
> **REOPENS:** SET_PROTOCOL(boot) is observed on the BIOS/KVM sample the owner treats as the target.

> **REFUSED:** widen the keyboard array and give up the boot-protocol truncation.
> **WHY:** this tree already implements the truncation, and deleting it is not a negative-cost change, so the only reason left to create a new spec deviation is that another project did.
> **REOPENS:** device evidence that the truncation branch itself causes a measured defect (for example report loss on a protocol-switch boundary).

D′ (widen, keep the boot cut, keep the NKRO toggle) is the only technically coherent upgrade and is strictly better than D: it loses no ceiling and does not touch 25 VIA files or the user docs. Still refused — the gain is default 6→20 against a permanent `report.h` fork and a 22-board descriptor change. D/D′ also do not remove the no-SET_PROTOCOL risk; they change the failure mode from no input to babble.

> **REFUSED:** grow `KEYBOARD_REPORT_KEYS` and `KEYBOARD_EPSIZE` for ERA and detach the keyboard report width from upstream.
> **WHY:** the gain is one default (6 → 20) against a ceiling that is already 240, and the cost is putting upstream-identical `tmk_core/protocol/report.h` on the permanent fork surface plus changing the USB descriptors of 22 shipped boards.
> **REOPENS:** the owner promotes "default rollover for a user who never opens VIA" to a product requirement, and the two owed measurements (SET_PROTOCOL sample, extended-report boot compatibility) pass. The choice then is D′, not D.

## Owed Measurements

Source cannot answer these. 1–2 are the entry conditions for reopening D′; 3 is live now.

| # | Question | How |
| --- | --- | --- |
| 1 | Does the owner's target BIOS/KVM sample send SET_PROTOCOL(boot)? | Enter a boot menu with NKRO on; input alive means that host sent SET_PROTOCOL. A USB analyzer is better |
| 2 | How far do hosts accept a 22-byte report on a boot-subclass interface? | Sister-project field evidence does not transfer. Re-test on the same sample as 1 — those are exactly the hosts that receive the extended report |
| 3 | After a descriptor change without a `usb.device_version` bump, does the host keep a cached parse? | Needs a reproduction on one shipped board |

Device falsifier for the pulse: NKRO on + KKUK on, hold two or more keys; the host must lose none.
