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
| `quantum/action_layer.c` | Composes the peer DUAL-HOST layer contribution at lookup without mutating local `layer_state`. | `ERA_SPLIT_PEER_LAYER_MERGE_ENABLE`. |
| `quantum/action_tapping.c` | Tap-hold decision seams and their diagnostic sinks for ERA split policy. | `SPECULATIVE_HOLD`, `ERA_SPECULATIVE_LAYER_ENABLE`, `ERA_SPLIT_TAP_ACTIVITY_ENABLE`. |
| `quantum/keycode_config.h` | Reserves the persisted inverted RGB Sleep master bit while preserving QMK's record width/default semantics. | Consumed under `ERA_RGB_SLEEP_MASTER_ENABLE`. |
| `quantum/eeconfig.h` | Quiet/deferred eeconfig helper so accepted continuous VIA saves can coalesce before physical NVM persistence. | `ERA_STORAGE_QUIET_DEFER_MS`. |
| `quantum/keyboard.[ch]` | Physical-switch event fanout plus pass-phase diagnostics used by ERA features. | Weak hooks are inert by default; diagnostics use `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE`. |
| `quantum/matrix.[ch]` | Raw/count diagnostic seams and the accepted debounce/post-scan separation. | Diagnostic gates named in source; structural split is ungated. `quantum/matrix_common.c` remains pristine. |
| `quantum/mousekey.[ch]` | Runtime-adjustable mousekey deltas plus value-preserving declarations/inert-task guard. | Runtime deltas under `ERA_MOUSEKEY_RUNTIME_DELTA`. |
| `quantum/process_keycode/process_tap_dance.[ch]`, `quantum/quantum.c` | Weak tap-dance policy seams, remap integration, synthesized-tap timing route, and ERA RGB Sleep suspend-entry gate. | Weak upstream defaults; RGB entry gate under `ERA_RGB_SLEEP_MASTER_ENABLE`. |
| `quantum/rgb_matrix/animations/digital_rain_anim.h` | Uses lib8tion RNG so the ERA image does not acquire newlib allocator state through this animation. | Ungated in this animation; QMK RGB Matrix already links lib8tion. |
| `quantum/rgb_matrix/rgb_matrix.[ch]` | Deferred persistence plus render-policy/idle/pass-phase hooks required by ERA presentation scheduling. | Storage/render/diagnostic gates remain source-owned. |
| `quantum/rgblight/rgblight.[ch]`, `quantum/rgblight/rgblight_modes.h` | Deferred RGBLight save, allocator-free RNG, and the ERA Pulse mode/dispatch/suspend integration. | Persistence/RNG/Pulse gates remain source-owned. |
| `quantum/split_common/split_util.[ch]` | Exposes physical-side sampling separately from QMK boot-master policy. | Present where `SPLIT_HAND_PIN` exists; weak QMK ownership remains default. |
| `quantum/sync_timer.[ch]` | Weak time-source decision so ERA split can use committed wire role instead of hardcoded keyboard mastery. | Weak default preserves upstream behavior outside ERA split. |
| `quantum/via.c` | Participates in accepted deferred RGB save and storage-cause instrumentation. | `ERA_STORAGE_QUIET_DEFER_MS` and the storage timeline diagnostic gate. |
| `tmk_core/protocol/chibios/usb_driver.c` | Stops IN retry/flush after USB leaves ACTIVE and exposes completed keyboard-report timing to ERA. | Disconnect/suspend guard is unconditional; report hook has its ERA gate. |
| `tmk_core/protocol/chibios/usb_report_handling.c` | Zero-fills unsupported GET_REPORT storage and bounds idle-report pacing. | Zero-fill unconditional; pacing follows `ERA_TIMER_MS_CACHE`. |
| `tmk_core/protocol/chibios/usb_main.c` | Routes keyboard reports through the ERA report-interval hold/post accounting and reports USB session edges. | `ERA_HID_REPORT_INTERVAL_ENABLE`. |
| `tmk_core/protocol/host.[ch]` | Weak keyboard-delay seam through which synthesized tap width reaches the transport. | Ungated weak default keeps upstream wait behavior. |
| `quantum/action.[ch]` | Routes synthesized keyboard-class tap width through the host delay seam. | Ungated; weak host default preserves behavior outside ERA. |
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
