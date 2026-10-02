# ERA QMK Fork Ledger

Genre: manual
Canonical for: the current ERA departures under QMK-owned source roots, the
reason/gate for each departure, the pristine comparison baseline, and the
upstream rebase boundary

The commit-time check in `hooks/era_commit_check.py` compares the staged index
under `quantum/`, `platforms/`, `tmk_core/`, `drivers/`, and
`builddefs/` with the pristine baseline below. It parses only the first column
of **Current Fork Edits** and requires exact agreement in both directions.

The vendored pristine QMK snapshot is named `c93ef27143`. Do not compare the
fork ledger against another ERA branch.

ERA-owned NVM protocol, split storage, and product policy belong under
`keyboards/era/common/`. A QMK-core departure is justified here only when the
required seam or behavior actually remains in a QMK-owned file.

## Current Fork Edits

| Files | Current ERA reason | Gate / scope |
| --- | --- | --- |
| `builddefs/common_features.mk` | Exposes QMK's lib8tion selection as a C define for RGBLight code that must know the linked RNG facility. | Existing `LIB8TION_ENABLE` make branch. |
| `platforms/bootloader.h`, `platforms/chibios/bootloaders/rp2040.c` | Non-blocking RP2040 double-tap classification/close path, including the pre-copy arm needed by SRAM-resident boot. | `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING`; pre-copy arm has its own gate. |
| `platforms/chibios/drivers/serial_protocol.h`, `platforms/chibios/drivers/serial_usart.c`, `platforms/chibios/drivers/vendor/RP/RP2040/serial_vendor.c` | Adds an upstream-shaped receive-timeout transport seam. ERA split itself uses custom transport. | No ERA selector; not linked by the ERA split transport. |
| `platforms/chibios/timer.c` | Millisecond cache for `timer_read32()` without changing QMK value/epoch semantics. | `ERA_TIMER_MS_CACHE`. |
| `quantum/action_layer.c` | Composes the peer DUAL-HOST layer contribution at lookup without mutating local `layer_state`; keeps momentary TD layer contributions out of ordinary layer state, so an unrelated release or a tri-layer update cannot drop or adopt them, while explicit replacement retires them. | Peer lookup: `ERA_SPLIT_PEER_LAYER_MERGE_ENABLE`. TD contributions: `TAP_DANCE_OWNED_ACTIONS`. |
| `quantum/action_tapping.[ch]` | One FIFO with local7/HP remote half-position quotas and origin accounting under `ERA_SPLIT_INPUT_HISTORY_ENABLE`; tap-hold decision seams and their diagnostic sinks for ERA split policy; full-width event deltas preserve exact uint16 terms even across a skipped 16-bit wrap. | `SPECULATIVE_HOLD`, `ERA_SPECULATIVE_LAYER_ENABLE`, `ERA_SPLIT_TAP_ACTIVITY_ENABLE`. |
| `quantum/keycode_config.h` | Reserves the persisted inverted RGB Sleep master bit while preserving QMK's record width/default semantics. | Consumed under `ERA_RGB_SLEEP_MASTER_ENABLE`. |
| `quantum/eeconfig.h` | Quiet/deferred eeconfig helper so accepted continuous VIA saves can coalesce before physical NVM persistence. | `ERA_STORAGE_QUIET_DEFER_MS`. |
| `quantum/keyboard.[ch]` | Whole-group HP history/closure admission before action passes under `ERA_SPLIT_INPUT_HISTORY_ENABLE`; physical-switch event fanout plus pass-phase diagnostics used by ERA features; 32-bit event timestamps and tick production preserve the original event time in queued tap-hold input. | Weak hooks are inert by default; diagnostics use `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE`. |
| `quantum/matrix.[ch]` | Raw/count diagnostic seams and the accepted debounce/post-scan separation. | Diagnostic gates named in source; structural split is ungated. `quantum/matrix_common.c` remains pristine. |
| `quantum/mousekey.[ch]` | Runtime-adjustable mousekey deltas plus value-preserving declarations/inert-task guard. | Runtime deltas under `ERA_MOUSEKEY_RUNTIME_DELTA`. |
| `quantum/process_keycode/process_tap_dance.[ch]`, `quantum/quantum.c` | TD state per physical input with captured release routing; queued-record identity and cancellation epochs, so a queued or late release never reaches a changed keymap; a dance's own records carry its position and run a quantum keycode as its output, never as another dance's input. Weak policy/timing seams; the suspend entries gate RGB on the RGB Sleep switch and, where ERA owns lighting sleep, leave backlight and RGB outputs to it. Left to QMK: an input clear from inside the waiting buffer also retires a later queued TD press. | `ERA_TAP_DANCE_ENABLE`: matrix-position seats, bounded pool, queued-record epochs. `TAP_DANCE_OWNED_ACTIONS`: output scopes. Policy hooks have inert defaults; RGB gate: `ERA_RGB_SLEEP_MASTER_ENABLE`; output ownership: `ERA_LIGHTING_SLEEP_OWNED`. |
| `quantum/rgb_matrix/animations/digital_rain_anim.h` | Uses lib8tion RNG so the ERA image does not acquire newlib allocator state through this animation. | Ungated in this animation; QMK RGB Matrix already links lib8tion. |
| `quantum/rgb_matrix/rgb_matrix.[ch]` | Deferred persistence plus render-policy/idle/pass-phase hooks required by ERA presentation scheduling. | Storage/render/diagnostic gates remain source-owned. |
| `quantum/rgblight/rgblight.[ch]`, `quantum/rgblight/rgblight_modes.h` | Deferred RGBLight save, allocator-free RNG, the ERA Pulse mode/dispatch/suspend integration, and an output-only sleep gate (`rgblight_set_output_suspend_state()`) that darkens every LED without touching `rgblight_config`. | Persistence/RNG/Pulse gates remain source-owned; the sleep gate is inert until ERA lighting sleep drives it. |
| `quantum/split_common/split_util.[ch]` | Exposes physical-side sampling separately from QMK boot-master policy. | Present where `SPLIT_HAND_PIN` exists; weak QMK ownership remains default. |
| `quantum/sync_timer.[ch]` | Weak time-source decision so ERA split can use committed wire role instead of hardcoded keyboard mastery. | Weak default preserves upstream behavior outside ERA split. |
| `quantum/via.c` | Participates in accepted deferred RGB save and storage-cause instrumentation. | `ERA_STORAGE_QUIET_DEFER_MS` and the storage timeline diagnostic gate. |
| `tmk_core/protocol/chibios/usb_driver.c` | Whole-buffer, nonblocking admission and successful-post counting under the Core0 USB lock for ERA keyboard/shared/mouse endpoints with retry owners; BUSY preserves the shared keyboard prefix. Independent RAW and other endpoints retain their timed transport. Stops IN retry/flush after USB leaves ACTIVE and exposes completed keyboard-report timing through `era_hid_report_interval_endpoint_completed_i()`. | Disconnect/suspend guard is unconditional; report hook has its ERA gate. |
| `tmk_core/protocol/chibios/usb_report_handling.c` | Zero-fills unsupported GET_REPORT storage and bounds idle-report pacing. | Zero-fill unconditional; pacing follows `ERA_TIMER_MS_CACHE`. |
| `tmk_core/protocol/chibios/usb_main.c` | Routes every keyboard void output into the interval owner, retains latest unaccepted auxiliary output without destructive BUSY fallback, exposes scalar pending work for the idle service gate, and reports USB session/protocol edges for lifetime checks at endpoint admission. | `ERA_HID_REPORT_INTERVAL_ENABLE`. |
| `tmk_core/protocol/host.[ch]` | Weak keyboard-delay seam through which synthesized tap width reaches the transport. | Ungated weak default keeps upstream wait behavior. |
| `quantum/action.[ch]` | Captures HP origin before swap-hands for FIFO accounting under `ERA_SPLIT_INPUT_HISTORY_ENABLE`; keeps synthesized tap width on `tap_code_wait()` and the `host_keyboard_delay()` seam; carries TD ingress identity and cancellation epoch through LT/MT records; passes an owned TD release as `ACTION_NO`, so a remapped entry cannot release another input's output; keeps QMK's forced re-press of a held usage as a report-only up; a retro tap adds only the tap-time modifiers that are up. | Tap width has an ungated weak default. Queued TD data: `ERA_TAP_DANCE_ENABLE`. Owned-release routing: `TAP_DANCE_ENABLE`. Output ownership: `TAP_DANCE_OWNED_ACTIONS`. |
| `quantum/action_util.[ch]` | Format-edge/current-owner force-send bypasses last_report under `ERA_HID_REPORT_INTERVAL_ENABLE`; separates TD modifier, HID-usage and mouse-code contributions from ordinary QMK bit state; a shared usage stays down until its last contribution is released. Report-only empty/restore stays distinct from input cancellation. The 6KRO and NKRO senders pass a copy through `keyboard_report_filter()` / `nkro_report_filter()`, which ERA SOCD uses to resolve on the outgoing report. | TD ownership: `TAP_DANCE_OWNED_ACTIONS`, ERA TD builds only; report filters are weak no-ops unless `ERA_SOCD_ENABLE` links one; ordinary keys, report formats and transport keep QMK semantics. |
| `tmk_core/protocol/usb_descriptor.[ch]` | Weak device/product/serial descriptor override hooks used by ERA split USB identity. | The three USB descriptor override gates. |

`drivers/sensors/cirque_pinnacle.c` contains identifiers named
`ERA_ReadBytes` and `ERA_WriteByte` where ERA means Cirque Extended Register
Access. That file is pristine and is not part of this ledger's difference set.

A row whose first-column path no longer differs from the pristine baseline is
stale. A differing file under the five checked roots with no row is an
undocumented fork. `hooks/era_commit_check.py` rejects either condition from
the staged index.

## Matrix Boundary

`contracts/era_invariants.md` owns the permitted matrix-core boundary and
`manuals/era_performance_gates.md` owns its Source Gate. The ledger records
the current files, not permission to add another matrix departure.

## Submodule Surface Outside The Five Directories

The five-root checker does not cover submodule pins. `lib/chibios` and
`lib/chibios-contrib` use the `custom/qmk-rp-usb` branch; the ChibiOS pin
supplies the remote-wake status hook consumed by ERA's RP2040 USB sleep path.
Re-derive this boundary with `git submodule status` and the branches recorded
in `.gitmodules`, not with the five-directory fork check.

## Tap Dance Decision Hooks

Tap Dance decision hooks `tap_dance_get_decision_term()` and
`tap_dance_hold_on_interrupt()` let ERA use captured per-slot hold thresholds
and decide an assigned hold before the interrupting key's source-layer lookup.
The weak core defaults keep the ordinary tapping term and tap-on-interrupt;
ERA's overrides leave legacy mode unchanged. Host evidence lives in
`tests/era_backlight_pulse/test_td_modes.h` and `tests/era_via_exact_ms/test_td_modes.h`.

## QMK CLI Surface Outside The Five Directories

The five-root checker does not cover `lib/python`. One departure lives there:
`lib/python/qmk/cli/generate/version_h.py` emits `QMK_BUILDDATE` only when the
build has not defined it, so `storage/era_via_magic.h` pins the VIA EEPROM
magic; other keyboards keep QMK's behavior. Re-derive this surface with
`git diff c93ef27143 -- lib/python`.


## Rebase Rule

For an upstream rebase or a QMK-core edit:

1. stage only the intended concern and let `hooks/era_commit_check.py` compare
   the five checked roots against `c93ef27143`;
2. reconcile **Current Fork Edits** in both directions in the same change;
3. confirm any proposed QMK-owned storage change has an independent current
   reason rather than resurrecting a retired storage ownership layer;
4. run the Source Gate and other affected checks named by
   `manuals/era_performance_gates.md`;
5. separately verify the submodule boundary when its pins or remote-wake seam
   are involved.

The ledger is a functional input to the commit gate, not a historical change
log.
