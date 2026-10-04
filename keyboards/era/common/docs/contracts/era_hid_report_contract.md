# ERA HID Keyboard Report Contract

Genre: contract
Canonical for: the HID keyboard report ERA sends the host — its shape and
width, the boot-protocol fallback that keeps BIOS working, the opt-in NKRO
policy, the refusals that keep report-array width and endpoint size tied to
the upstream values, and the rule that any code that empties and restores
the report buffer must follow the active format

VIA NKRO value-id owner: `features/era_nkro_via.h`; namespace locator:
`era_identifier_map.md`. Core-file fork surface:
`era_qmk_fork_ledger.md`. HOST-PEER carries matrix rows, not this report:
`era_host_peer_matrix_contract.md`.

Owner decision: keep A — a spec 6-key report plus an opt-in NKRO toggle,
both default off. The KKUK pulse is independent of that choice.

## Report shape

ERA intentionally keeps QMK's upstream boot-keyboard report shape instead of
owning a second field/constant table here. The source owners are
`report_keyboard_t`, `KEYBOARD_REPORT_KEYS`, and `KEYBOARD_REPORT_SIZE` in
`tmk_core/protocol/report.h`, plus `KeyboardReport[]`, the keyboard interface
and descriptor, polling interval, and `KEYBOARD_EPSIZE` in
`tmk_core/protocol/usb_descriptor.c` and `tmk_core/protocol/usb_descriptor.h`.
ERA does not define `KEYBOARD_SHARED_EP`.

The contract requirement is the standard six-key boot-compatible keyboard
report as the default path, with NKRO as a separate opt-in format. A change to
the default width, report id, or keyboard endpoint capacity reopens **Width**
and the boot-compatibility decision below.

`tmk_core/protocol/report.h` is not on the fork surface in
`era_qmk_fork_ledger.md`. `tmk_core/protocol/usb_descriptor.c` and
`tmk_core/protocol/usb_descriptor.h` are, for identity override hooks, not
for width.

## Boot and report protocol

The USB protocol state machine and request fields are source-owned by
`tmk_core/protocol/usb_device_state.c` and
`tmk_core/protocol/chibios/usb_main.c`; this contract does not duplicate their
event/field table. Power-on and USB reset start in report protocol, and only
the keyboard interface may answer or change keyboard protocol state.

`send_keyboard()` in `tmk_core/protocol/chibios/usb_main.c` MUST retain the
explicit boot-protocol path required by HID 1.11 §7.2.6. In boot protocol it
sends the boot-compatible eight-byte keyboard report; in report protocol it
uses QMK's current keyboard report size. A wider report or report id therefore
must preserve the boot cut rather than silently extending boot protocol.
`usb_get_report_cb()` in `tmk_core/protocol/chibios/usb_report_handling.c` and
`usb_endpoint_in_send()` in `tmk_core/protocol/chibios/usb_driver.c` keep
GET_REPORT tied to the bytes actually queued.

> **REFUSED:** delete the boot branch of `send_keyboard()` and always send the extended report.
> **WHY:** that branch is the HID 1.11 §7.2.6 compliance this firmware already ships; deleting it saves a few lines and loses correctness on every host that actually negotiates boot protocol.
> **REOPENS:** evidence that variable-length send has a measurable cost — on this tree's 1 ms poll and queue-based send, length is only an argument, so that evidence is not here.

## NKRO

The NKRO report fields, bit count, shared endpoint, report id, and build
wiring are source-owned by `report_nkro_t` in `tmk_core/protocol/report.h`,
`tmk_core/protocol.mk`, `tmk_core/protocol/usb_descriptor.h`, and
`tmk_core/protocol/chibios/usb_endpoints.c`. The live format-selection
predicate is source-owned by `send_keyboard_report()` in
`quantum/action_util.c` and `host_can_send_nkro()` in
`tmk_core/protocol/host.c`.

The product rule is narrower: NKRO is opt-in and default-off, and it is used
only while the host permits report protocol. A host that negotiates boot
protocol must fall back to the six-key keyboard report. The persisted choice
is QMK's `keymap_config.nkro`; its layout and defaulting stay QMK-owned rather
than being mirrored here.

On a host that never sends SET_PROTOCOL the state stays
`USB_PROTOCOL_REPORT`, so `host_can_send_nkro()` (`tmk_core/protocol/host.c`)
is true, and if `keymap_config.nkro` is on every key leaves only on the
shared endpoint. A host that opened only the boot keyboard interface then
sees **no input at all**. Which BIOS/KVM do this is an owed measurement;
`user/usevia.txt` tells the owner NKRO is off by default and to turn it off
again when an old BIOS, KVM or boot menu cannot read the keyboard.

> **REFUSED:** ship NKRO on by default via `NKRO_DEFAULT_ON`.
> **WHY:** on a host that never sends SET_PROTOCOL nothing leaves the keyboard interface, so input disappears, and the gain is a default that applies only after `eeconfig_init_quantum()` (`quantum/eeconfig.c`) — boards that already stored eeconfig stay 6KRO.
> **REOPENS:** SET_PROTOCOL(boot) is observed on the BIOS/KVM sample the owner treats as the target.

## Boards and VIA

The current board/keymap inventory is source-owned and is not counted here.
The common build gate is `system/era_common_qmk_rules.mk`: under VIA it derives
the NKRO adapter from QMK's `NKRO_ENABLE` rather than introducing a second ERA
selector. `sirind/brick65/post_rules.mk` and
`sirind/brick65/keymaps/via/BRICK65-VIA.json` remain the atmega32u4 exception
and do not expose this FEATURE surface.

The live VIA value id is source-owned by `features/era_nkro_via.h`;
`era_identifier_map.md` locates the competing namespace owners.
`features/era_nkro_via.c`
owns no independent state: it writes QMK's `keymap_config.nkro` and clears the
keyboard on both sides of a format switch so a held key cannot be stranded in
the old report format. There is no delayed-save arm.

KKUK's current menu fields, channel, and mode constant are source-owned by the
VIA JSON, `features/era_kkuk.h`, `features/era_kkuk_via.c`, and
`storage/era_eeprom_layout.h`. The compatibility rule remains: current JSON
does not expose the obsolete one-option Mode control, while firmware retains
its legacy value-id handling for cached older VIA definitions.

## KKUK pulse

`era_kkuk_start_empty_pulse()` in `features/era_kkuk.c` releases held
ordinary keys except the usages of active, non-overlapping SOCD pairs.
Those usages retain their raw held state in the pulse, so SOCD chooses the
same winner without inventing a release/re-press. Disabled or invalid pairs
have no exemption. Modifiers are never cleared. The next task tick's
`era_kkuk_finish_restore_pulse()` calls `send_keyboard_report()`
(`quantum/action_util.c`) with the current report, not an old snapshot;
a physical release between the two ticks therefore stays released. The snapshot
must be the report `clear_keys()` will empty, and that is not always 6KRO:
`clear_keys_from_report()` in `tmk_core/protocol/report.c` writes
`nkro_report->bits` and `keyboard_report->keys` together on ERA NKRO builds.
The pulse saves both arrays and restores both, so both projections retain
the same live owner state across a format edge. Both snapshots are function-local; only `kkuk_pulse_state`
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

## Tap width and the report interval

A synthesized tap has a host-visible width: Caps Lock is held
`TAP_HOLD_CAPS_DELAY` (80 ms unless a board sets it), because macOS ignores a
shorter Caps tap; every other usage is held `TAP_CODE_DELAY`. Every path that
synthesizes a keyboard-class tap asks for that width through
`tap_code_wait()` in `quantum/action.c` — the Layer-Tap and Mod-Tap release,
swap-hands, retro tapping, the Caps Word fallback, `tap_code_delay()`,
`tap_code16_delay()` in `quantum/quantum.c`, and the Tap Dance slot's
`features/era_tapdance.c` — and none of them waits for it. `tap_code_wait()`
sends a keyboard-class usage (basic, modifier, or a pure-modifier `QK_MODS`)
to `host_keyboard_delay()`; a system, consumer or mouse usage keeps QMK's
synchronous wait, since it rides no keyboard report. The class test is the
H7S firmware's, so the sister tree routes the same codes the same way.

**The width lives on the USB transport as a minimum report interval.**
`host_keyboard_delay()`'s weak default in `tmk_core/protocol/host.c` is the
wait QMK always performed; on every ERA image the strong definition in
`system/era_hid_report_interval_chibios.c` hands the request to
`system/era_hid_report_interval.c`, which holds the next keyboard-class
report, and every keyboard report behind it in order, until at least the
requested time has passed since the latest keyboard-class report reached the
host. QMK's logical release is registered at once; only the report that
carries it waits, so no later key-up exists that could release a separately
held physical key with the same usage. Mouse and EXTRA reports are never
held; on the shared endpoint they are counted so completion order stays
exact. `send_keyboard()` and `send_nkro()` in
`tmk_core/protocol/chibios/usb_main.c` ask the hold before posting,
the whole-buffer admission hook counts every successful post under the USB
lock, and the IN completion in
`tmk_core/protocol/chibios/usb_driver.c` stamps the moment a report reached
the host. `era_common_features_task()` in `system/era_common_features.c`
services the unit once per pass, including format/session re-open obligations. The scan loop never stalls for a width. Boards outside the ERA
layer, and the host test platform, keep the weak wait.

| Rule | Where |
| --- | --- |
| The interval is measured from the completion of the report the tap posted; time already elapsed counts toward it; repeated requests take the larger width | `era_hid_report_interval_request()` in `system/era_hid_report_interval.c` |
| A report held behind an interval keeps the width it carries, so back-to-back synthesized taps stay their full width apart | the backlog entry's `delay_after` in `system/era_hid_report_interval.c` |
| Preserve the accepted FIFO prefix; merge only overflow reports into a separate latest suffix; move the suffix after the prefix and its final width drain | `ERA_HID_REPORT_INTERVAL_BACKLOG` in `system/era_hid_report_interval.h` |
| A completion that never arrives abandons the width after `ERA_HID_REPORT_INTERVAL_ANCHOR_LIMIT_MS` and the backlog drains: a host that stopped polling cannot wedge input | `era_hid_report_interval_task()` in `system/era_hid_report_interval.c` |
| USB reset, unconfigure and configure drop the backlog and every interval with the queues they were behind; suspend keeps the backlog and measures a pending width from resume | `usb_event_cb()` in `tmk_core/protocol/chibios/usb_main.c`, `era_hid_report_interval_note_session_edge()` in `system/era_hid_report_interval.c` |
| A requested width is capped at `ERA_HID_REPORT_INTERVAL_REQUEST_MAX_MS` | `era_hid_report_interval_request()` in `system/era_hid_report_interval.c` |

> **REFUSED:** schedule the logical key-up of a synthesized tap on a timer instead of holding its report.
> **WHY:** QMK's report state does not distinguish a synthesized usage from a physically held key with the same usage, so a deferred key-up can release the physical key or a later press of the same code; holding the report keeps every logical transition where QMK made it.
> **REOPENS:** a synthesized-versus-physical usage ownership model with host-visible overlap proof.

> **REFUSED:** hold the shared endpoint's queue as a whole for the interval.
> **WHY:** the shared endpoint also carries mouse reports, and 80 ms of held mouse motion after every Caps tap is a visible stall; the width is a keyboard fact and only keyboard-class reports wait.
> **REOPENS:** never, while mouse reports share that endpoint.

## Bounded prefix and USB acceptance

Every valid keyboard/NKRO void output is owned by the interval unit, including
normal endpoint BUSY, because upper `last_report` has already advanced. A report
ticket attaches width to that producer's report. Prefix widths survive; a
replaced suffix loses its own width rather than accumulating it on an unrelated
report. First suffix overflow emits a sticky pressure edge and prevents future
HP input claims; local/timer/already claimed work continues. After materializing
the suffix, capacity recovers through a fresh input baseline, not old history.
Input reset never deletes this mixed prefix or suffix.

The required finite preservation profiles, with zero posting in the same
window, are Caps release + four basic taps + basic TD double fallback + modifier
pair (15), Caps release + two TD double fallbacks + four basic taps (17), local
MT/LT tap + eight remote basic keys down/up (18), and Caps release + MT tap +
eight remote basic keys down/up + basic TD double fallback + modifier pair
(25 nominal). `tests/era_split_input_qmk/` counts real production QMK/TD reports,
including NKRO for the eight-key chord; the linked capacity is
`ERA_HID_REPORT_INTERVAL_BACKLOG`. This finite profile is not an upper bound
on all TD, macro, filter or firmware output. Ordinary boot width and NKRO
opt-in are unchanged; larger bursts use suffix convergence.

Only USB_ACTIVE, nonsuspended, nonfull, no-partial-buffer, whole empty-buffer
copy/post under one Core0 system lock counts as success. Keyboard and shared
endpoint admission rechecks pending USB lifetime edges inside that lock;
keyboard admission also rejects a stale format, including a protocol change
that returns to the previous format before the task runs. Keyboard and shared
mouse/EXTRA producers cannot reset that queue on normal BUSY. Mouse, system,
consumer and programmable-button owners retain an unaccepted latest report
per producer and retry independently of keyboard intervals; this convergence
policy does not promise every auxiliary motion or tap during overload.

Prefix obligations are within one USB session/format. Reset/unconfigure end
the old obligations; suspend keeps them and starts missing-completion width
from active resume. Format changes end held old-format tap obligations without
clearing mouse/EXTRA or completion accounting for reports already accepted by
USB. A local NKRO choice additionally owes a terminal empty to the old interface,
preserved through normal BUSY before new keyboard state is admitted. A host
protocol change ends the old format lifetime; it does not owe that local-toggle
empty. Reset/unconfigure end both obligations, while suspend preserves them.
Stored idle reports may not resend a keyboard payload through an inactive format.
Reopen force-sends current filtered QMK owner state through
the selected format, bypassing upper duplicate suppression. Already accepted
old-format endpoint reports cannot be retracted; cross-format tap preservation
is not promised. Device IN/OS completion and original global event durations
remain separately measured boundaries.

## Split and storage

The HID report itself never crosses the split wire. Report-buffer touch sites
are implementation-owned by `features/era_kkuk.c` and the HID re-open path in
`split/era_split_authority_reducer.c`; this contract does not mirror their
call-site inventory. The persisted NKRO choice is part of QMK
`keymap_config`; its split-storage domain size is source-owned by
`split/era_host_peer_storage.h`, and board enablement is owned by each board's
build rules. Changing HID report width does not change the split wire schema or
that persisted domain.

## Width

A wider six-key-format replacement must fit the keyboard endpoint buffer:
`usb_endpoint_in_send()` in `tmk_core/protocol/chibios/usb_driver.c` refuses a
report larger than that endpoint's configured buffer, while the current
keyboard and shared endpoint capacities are source-owned by
`tmk_core/protocol/usb_descriptor.h` and their descriptors by
`tmk_core/protocol/usb_descriptor.c`. NKRO already supplies the higher-rollover
path on its shared endpoint. Widening the default report is therefore a
keyboard-endpoint/descriptor change, not a shared-endpoint change.

> **REFUSED:** widen the keyboard array and give up the boot-protocol truncation.
> **WHY:** this tree already implements the truncation, and deleting it is not a negative-cost change, so the only reason left to create a new spec deviation is that another project did.
> **REOPENS:** device evidence that the truncation branch itself causes a measured defect (for example report loss on a protocol-switch boundary).

D′ (widen, keep the boot cut, keep the NKRO toggle) is the only technically
coherent upgrade and is strictly better than D: it preserves the opt-in
higher-rollover path and does not require changing the VIA toggle contract or
user docs. Still refused — the gain is default 6→20 against a permanent
`report.h` fork and descriptor changes across shipped boards. D/D′ also do not
remove the no-SET_PROTOCOL risk; they change the failure mode from no input to
babble.

> **REFUSED:** grow `KEYBOARD_REPORT_KEYS` and `KEYBOARD_EPSIZE` for ERA and detach the keyboard report width from upstream.
> **WHY:** the gain is one wider default against a higher-rollover path that already exists behind opt-in NKRO, and the cost is putting upstream-identical `tmk_core/protocol/report.h` on the permanent fork surface plus changing shipped USB descriptors.
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
