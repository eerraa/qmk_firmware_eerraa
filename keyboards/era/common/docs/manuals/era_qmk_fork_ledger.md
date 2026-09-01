# ERA QMK Fork Ledger

Genre: manual
Canonical for: every current ERA edit under QMK-owned source directories, why
it exists, the gate it rides, and how the whole five-directory surface is
re-derived against the vendored pristine snapshot

The commit-time check in `hooks/era_commit_check.py` compares the staged index
under `quantum/`, `platforms/`, `tmk_core/`, `drivers/`, and `builddefs/` with
the pristine snapshot named below. Every differing path must appear in
**Current Fork Edits**. The ledger is read from the staged index too, so neither
an unstaged core file nor an unstaged ledger can change the commit's answer.

The vendored pristine QMK snapshot is named `c93ef27143`. Do not compare against
another ERA branch.

ERA NVM sits below QMK's supported `EEPROM_DRIVER=custom` boundary. A/B banks,
macro durability, local changed-span notification, remote Apply and CLEAN replay
proof live under `keyboards/era/common/`, not in QMK Core.

## Current Fork Edits

| Files | Current ERA reason | Gate / scope |
| --- | --- | --- |
| `builddefs/common_features.mk` | Emits `LIB8TION_ENABLE` as a C define beside QMK's existing lib8tion source selection, so RGBLight can choose lib8tion RNG without guessing feature composition. | Inside existing `LIB8TION_ENABLE` make branch. |
| `platforms/bootloader.h`, `platforms/chibios/bootloaders/rp2040.c` | ERA's non-blocking RP2040 double-tap bootloader window: pre-copy CLEAR/ARMED/REQUEST classification, loop-driven close, and software-reset/bootloader disarm. | `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING`; pre-copy arm additionally `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM`. |
| `platforms/chibios/drivers/serial_protocol.h`, `platforms/chibios/drivers/serial_usart.c`, `platforms/chibios/drivers/vendor/RP/RP2040/serial_vendor.c` | Upstream-shaped `serial_transport_receive_timeout()` in `platforms/chibios/drivers/serial_protocol.h`. ERA split does not link these serial implementations. | No ERA selector; ERA split uses `SPLIT_TRANSPORT=custom`. |
| `platforms/chibios/timer.c` | Once-per-millisecond `timer_read32()` cache preserving QMK's value/epoch semantics while avoiding repeated RP2040 64-bit conversion work. | `ERA_TIMER_MS_CACHE`. |
| `quantum/action_layer.c` | OR-composes the peer's DUAL-HOST layer contribution at action lookup without mutating local `layer_state`. | `ERA_SPLIT_PEER_LAYER_MERGE_ENABLE`. |
| `quantum/action_tapping.c` | Speculative-hold / cross-half tap-hold judgment seams and associated diagnostic sinks. | `SPECULATIVE_HOLD`, `ERA_SPECULATIVE_LAYER_ENABLE`, `ERA_SPLIT_TAP_ACTIVITY_ENABLE`. |
| `quantum/keycode_config.h` | Allocates one formerly-unused keymap-config bit as inverted `era_rgb_sleep_disabled`, preserving the 2-byte QMK record and zero/default semantics while giving every ERA RGB board one persisted master. | Bit meaning is consumed only under `ERA_RGB_SLEEP_MASTER_ENABLE`; record size remains statically 2 bytes. |
| `quantum/eeconfig.h` | Generic quiet/deferred eeconfig helper used for accepted RGB Matrix persistence coalescing. Kept after the NVM cutover: stock restoration would turn each VIA slider SAVE into an immediate physical NVM write. | `ERA_STORAGE_QUIET_DEFER_MS`. |
| `quantum/keyboard.c` | Pass-phase diagnostic marks only. The storage-driven `matrix_task()` export is gone; `quantum/keyboard.h` is identical to the vendored pristine snapshot. | `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE`. |
| `quantum/matrix.[ch]` | Stock-matrix raw/count diagnostic hooks plus the accepted debounce/matrix-post-scan separation described by `era_invariants.md`. | `MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE`, `MATRIX_SCAN_COUNT_DIAGNOSTICS_ENABLE`; structural split is ungated. `quantum/matrix_common.c` remains identical to the vendored pristine snapshot. |
| `quantum/mousekey.[ch]` | Inert-task early-out in `quantum/mousekey.c`, missing runtime-variable declarations, and ERA-adjustable default accelerated-mode movement/wheel deltas. | Runtime deltas: `ERA_MOUSEKEY_RUNTIME_DELTA`; inert guard/declarations are behaviour-preserving ungated fixes. |
| `quantum/process_keycode/process_tap_dance.[ch]`, `quantum/quantum.c` | Weak `tap_dance_remap_keycode()` / `tap_dance_get_tapping_term()` in `quantum/process_keycode/process_tap_dance.c`; `process_record_quantum()` applies the remap both before preprocessing and after a layer-changing relookup. The same `quantum.c` also gates RGBLight/RGB Matrix **suspend entry only** on the ERA RGB Sleep master, while wake remains unconditional to clear stale darkness. | Tap Dance uses weak upstream-defaulted hooks; RGB gate is `ERA_RGB_SLEEP_MASTER_ENABLE`. |
| `quantum/rgb_matrix/animations/digital_rain_anim.h` | Replaces `rand()` with lib8tion `random16()` so ERA images do not pull newlib allocator state. Ungated in this file. RGB Matrix already sets `LIB8TION_ENABLE := yes` in `builddefs/common_features.mk`. | Ungated `random16()`. |
| `quantum/rgb_matrix/rgb_matrix.[ch]` | Accepted RGB Matrix persistence coalescing plus ERA render policy/idle and pass-phase performance hooks. Board-policy refresh and idle-wake live in `quantum/rgb_matrix/rgb_matrix.c`; read-only `rgb_matrix_render_policy_refresh_active()` remains true through the refreshed PWM flush or a no-frame update. Presentation priority only; not NVM ownership. | `ERA_STORAGE_QUIET_DEFER_MS`, `RGB_MATRIX_RENDER_POLICY_ENABLE`, `RGB_MATRIX_IDLE_GATE_ENABLE`, `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE`; the small value-preserving performance guards are ungated. |
| `quantum/rgblight/rgblight.[ch]` | Accepted VIA RGBLight quiet-save gate with flush-before-reset/suspend support; lib8tion RNG substitution avoids allocator linkage. | Persistence: `ERA_STORAGE_QUIET_DEFER_MS`; RNG: `LIB8TION_ENABLE`. |
| `quantum/split_common/split_util.[ch]` | Extracts `split_hand_pin_is_left()` in `quantum/split_common/split_util.c` so ERA authority can sample the physical side without adopting QMK's boot-master policy. | Present only where `SPLIT_HAND_PIN` exists; weak upstream `is_keyboard_left_impl()` still delegates to it. |
| `quantum/sync_timer.[ch]` | Replaces hardcoded keyboard-master time-source decisions with weak `sync_timer_is_time_source()` in `quantum/sync_timer.c`, defaulting to upstream mastery; ERA split overrides it with committed wire role. | Weak default; only ERA split scheduler changes semantics. |
| `quantum/via.c` | Accepted deferred SAVE participation for QMK RGB Matrix/RGBLight channels plus cause-variant macro timing instrumentation. Dynamic macro persistence itself is stock QMK again. | Persistence: `ERA_STORAGE_QUIET_DEFER_MS`; timing: `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE`. |
| `tmk_core/protocol/chibios/usb_driver.c` | Stops IN endpoint resend/flush work when USB is no longer ACTIVE, avoiding an unbounded retry across disconnect/suspend transitions. | Ungated ChibiOS fix. |
| `tmk_core/protocol/chibios/usb_report_handling.c` | Zero-fills unsupported GET_REPORT storage and paces host-requested idle-report walks to millisecond cadence. | Zero-fill ungated; pacing under `ERA_TIMER_MS_CACHE`. |
| `tmk_core/protocol/usb_descriptor.[ch]` | Weak device/product/serial descriptor override hooks used by ERA split USB identity. | `USB_DEVICE_DESCRIPTOR_OVERRIDE_ENABLE`, `USB_PRODUCT_STRING_OVERRIDE_ENABLE`, `USB_SERIAL_NUMBER_STRING_OVERRIDE_ENABLE`. |

`drivers/sensors/cirque_pinnacle.c` contains `ERA_ReadBytes` and
`ERA_WriteByte`, where ERA means Cirque's Extended Register Access, not this
firmware. The file is identical to the vendored pristine snapshot and therefore
does not enter the staged-index difference set.

A row whose files no longer differ from that snapshot is stale. A five-directory
edit with no row is an undocumented fork. The hook parses only the first column
of **Current Fork Edits**, expands `.[ch]`, and refuses either mismatch: a
difference with no row or a row whose files have returned to pristine.

## Storage-Specific QMK Surface Restored

The following storage-driven QMK files/edits are identical to the vendored
pristine snapshot and therefore **do not belong in the current fork surface**:
`drivers/eeprom/eeprom_driver.h`; `drivers/eeprom/eeprom_wear_leveling.c`;
`platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash.c`;
`quantum/wear_leveling/wear_leveling.c` and `.h`;
`quantum/wear_leveling/tests/rules.mk` and
`quantum/wear_leveling/tests/wear_leveling_general.cpp`;
`quantum/nvm/eeprom/nvm_dynamic_keymap.c` and
`quantum/nvm/nvm_dynamic_keymap.h`; `quantum/nvm/eeprom/nvm_eeconfig.c`;
`quantum/nvm/eeprom/nvm_eeprom_eeconfig_internal.h`;
`quantum/nvm/eeprom/nvm_via.c`; `quantum/keyboard.h` and the storage-only
`matrix_task()` visibility change formerly paired with it.

The stock QMK dynamic-macro RESET loop is now deliberately a dependency rather
than a fork edit. ERA NVM recognizes its sequential 16-byte
`eeprom_update_block()` transcript below QMK Core. The exact constraint and the
regression that compiles stock `nvm_dynamic_keymap.c` itself are canonical in
`era_host_peer_storage_contract.md` and `tests/era_nvm_qmk_driver`.

The retained QMK quiet-save edits are not hidden storage ownership. They remain
for one accepted behavior: VIA emits SAVE alongside continuous slider SET
traffic, and the ERA gate coalesces that stream and flushes a pending save
before controlled reset/suspend.

> **REFUSED:** restore the quiet-save QMK edits to pristine to shrink the fork
> **WHY:** VIA SAVE rides continuous slider SET traffic, and stock persistence writes each slider step durably.
> **REOPENS:** an explicit product decision to accept the extra physical writes, or a cleaner QMK-external interception

## Matrix Boundary

`era_invariants.md` remains canonical for `quantum/matrix.c` and
`quantum/matrix_common.c`. The latter is identical to the vendored pristine
snapshot. The former contains only the documented diagnostic hooks and the
accepted debounce/post-scan separation.
Do not infer permission for additional matrix-core edits from the existence of
those changes.

## Submodule Surface Outside The Five Directories

One ERA platform edit is outside `tmk_core/`, `quantum/`, `platforms/`,
`drivers/`, and `builddefs/` because it is a submodule pin. `lib/chibios` and
`lib/chibios-contrib` use the `custom/qmk-rp-usb` branch; the ChibiOS pin
supplies the remote-wake status hook consumed by the ERA RP2040 USB sleep
synchronization path. Re-derive that with `git submodule status` and the branch
recorded in `.gitmodules`, not with the five-directory commit check.

## Rebase Rule

For every rebase or QMK-core edit:

1. let `hooks/era_commit_check.py` compare the staged five-directory index with
   the pristine snapshot named above;
2. reconcile every current fork file to this table in both directions, including
   removal of a row whose file returned to pristine;
3. verify that the storage-restoration list remains absent unless a newly
   approved independent QMK reason is documented;
4. run the Source Gate and the tests in `era_performance_gates.md`;
5. update this ledger in the same change as any QMK-core movement.
