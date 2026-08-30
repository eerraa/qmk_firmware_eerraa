# ERA QMK Fork Ledger

Genre: manual
Canonical for: every current ERA edit under QMK-owned source directories, why
it exists, the gate it rides, and how to re-derive the surface against pristine
upstream

The pristine reference is QMK commit `c93ef27143`. On a development working
tree the authoritative current surface is:

```text
git diff --name-only c93ef27143 -- tmk_core quantum platforms drivers builddefs
```

After the implementation is committed, the equivalent tip check is:

```text
git diff --name-only c93ef27143 HEAD -- tmk_core quantum platforms drivers builddefs
```

Do not substitute another ERA branch or a repository root commit. QMK history
has multiple roots and those answer a different question.

Session 2 deliberately restores storage-specific QMK forks to pristine. ERA
NVM is below QMK's supported `EEPROM_DRIVER=custom` boundary, so A/B banks,
macro durability, local changed-span notification, remote Apply and CLEAN replay
proof live under `keyboards/era/common/`, not in QMK Core.

## Current Working-Tree Surface

At this Session 2 working tree the five-directory derivation contains **32
files**. Every one is accounted for below.

| Files | Current ERA reason | Gate / scope |
| --- | --- | --- |
| `builddefs/common_features.mk` | Emits `LIB8TION_ENABLE` as a C define beside QMK's existing lib8tion source selection, so RGBLight can choose lib8tion RNG without guessing feature composition. | Inside existing `LIB8TION_ENABLE` make branch. |
| `platforms/bootloader.h`, `platforms/chibios/bootloaders/rp2040.c` | ERA's non-blocking RP2040 double-tap bootloader window: pre-copy CLEAR/ARMED/REQUEST classification, loop-driven close, and software-reset/bootloader disarm. | `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING`; pre-copy arm additionally `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM`. |
| `platforms/chibios/drivers/serial_protocol.h`, `serial_usart.c`, `drivers/vendor/RP/RP2040/serial_vendor.c` | Upstream-shaped `serial_transport_receive_timeout()` extension. ERA's custom split transport does not link these serial implementations, but the fork carries the API extension for other QMK users. | No ERA selector; ERA split itself uses `SPLIT_TRANSPORT=custom`. |
| `platforms/chibios/timer.c` | Once-per-millisecond `timer_read32()` cache preserving QMK's value/epoch semantics while avoiding repeated RP2040 64-bit conversion work. | `ERA_TIMER_MS_CACHE`. |
| `quantum/action_layer.c` | OR-composes the peer's DUAL-HOST layer contribution at action lookup without mutating local `layer_state`. | `ERA_SPLIT_PEER_LAYER_MERGE_ENABLE`. |
| `quantum/action_tapping.c` | Speculative-hold / cross-half tap-hold judgment seams and associated diagnostic sinks. | `SPECULATIVE_HOLD`, `ERA_SPECULATIVE_LAYER_ENABLE`, `ERA_SPLIT_TAP_ACTIVITY_ENABLE`. |
| `quantum/eeconfig.h` | Generic quiet/deferred eeconfig helper used for accepted RGB Matrix persistence coalescing. This remains intentionally after the NVM cutover: stock restoration would turn each VIA slider SAVE into an immediate physical NVM write. | `ERA_STORAGE_QUIET_DEFER_MS`. |
| `quantum/keyboard.c` | Pass-phase diagnostic marks only. The storage-driven `matrix_task()` export is gone; `quantum/keyboard.h` is pristine again. | `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE`. |
| `quantum/matrix.c`, `quantum/matrix.h` | Stock-matrix raw/count diagnostic hooks plus the accepted debounce/matrix-post-scan separation described by `era_invariants.md`. | `MATRIX_SCAN_RAW_DIAGNOSTICS_ENABLE`, `MATRIX_SCAN_COUNT_DIAGNOSTICS_ENABLE`; structural split is ungated. `quantum/matrix_common.c` remains pristine. |
| `quantum/mousekey.c`, `quantum/mousekey.h` | Inert-task early-out, missing runtime-variable declarations, and ERA-adjustable default accelerated-mode movement/wheel deltas. | Runtime deltas: `ERA_MOUSEKEY_RUNTIME_DELTA`; inert guard/declarations are behaviour-preserving ungated fixes. |
| `quantum/process_keycode/process_tap_dance.c`, `.h`, `quantum/quantum.c` | Weak tap-dance keycode remap and tapping-term seams; `process_record_quantum()` applies the remap both before preprocessing and after a layer-changing relookup. | Weak upstream-defaulted hooks; ERA feature supplies the override. |
| `quantum/rgb_matrix/animations/digital_rain_anim.h` | Uses lib8tion RNG when available so ERA images do not pull newlib allocator state through `rand()`. | `LIB8TION_ENABLE`; other keyboards retain QMK fallback. |
| `quantum/rgb_matrix/rgb_matrix.c`, `quantum/rgb_matrix/rgb_matrix.h` | Accepted RGB Matrix persistence coalescing plus ERA render policy/idle and pass-phase performance hooks. The render-policy surface includes an explicit board-policy refresh request: a STATUS edge that arrives outside `rgb_matrix_task()` wakes the idle state on the next pass, or follows an already-buffered flush immediately, instead of waiting for the next animation epoch. Its read-only `rgb_matrix_render_policy_refresh_active()` remains true through the actual refreshed PWM flush (or an update that proves no frame is needed), which lets ERA's opportunistic NVM maintenance yield instead of inserting flash windows between a split indicator's multi-pass render states. The query carries presentation priority only; it is not NVM ownership. | `ERA_STORAGE_QUIET_DEFER_MS`, render-policy selectors, `RGB_MATRIX_IDLE_GATE_ENABLE`, `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE`; the small value-preserving performance guards are ungated. |
| `quantum/rgblight/rgblight.c`, `quantum/rgblight/rgblight.h` | Accepted VIA RGBLight quiet-save gate with flush-before-reset/suspend support; lib8tion RNG substitution avoids allocator linkage. | Persistence: `ERA_STORAGE_QUIET_DEFER_MS`; RNG: `LIB8TION_ENABLE`. |
| `quantum/split_common/split_util.c`, `.h` | Extracts `split_hand_pin_is_left()` so ERA authority can sample the physical side without adopting QMK's boot-master policy. | Present only where `SPLIT_HAND_PIN` exists; weak upstream `is_keyboard_left_impl()` still delegates to it. |
| `quantum/sync_timer.c`, `.h` | Replaces hardcoded keyboard-master time-source decisions with weak `sync_timer_is_time_source()`, defaulting to upstream mastery; ERA split overrides it with committed wire role. | Weak default; only ERA split scheduler changes semantics. |
| `quantum/via.c` | Accepted deferred SAVE participation for QMK RGB Matrix/RGBLight channels plus cause-variant macro timing instrumentation. Dynamic macro persistence itself is stock QMK again. | Persistence: `ERA_STORAGE_QUIET_DEFER_MS`; timing: `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE`. |
| `tmk_core/protocol/chibios/usb_driver.c` | Stops IN endpoint resend/flush work when USB is no longer ACTIVE, avoiding an unbounded retry across disconnect/suspend transitions. | Ungated ChibiOS fix. |
| `tmk_core/protocol/chibios/usb_report_handling.c` | Zero-fills unsupported GET_REPORT storage and paces host-requested idle-report walks to millisecond cadence. | Zero-fill ungated; pacing under `ERA_TIMER_MS_CACHE`. |
| `tmk_core/protocol/usb_descriptor.c`, `.h` | Weak device/product/serial descriptor override hooks used by ERA split USB identity. | `USB_DEVICE_DESCRIPTOR_OVERRIDE_ENABLE`, `USB_PRODUCT_STRING_OVERRIDE_ENABLE`, `USB_SERIAL_NUMBER_STRING_OVERRIDE_ENABLE`. |

The table accounts for all 32 current files. A future current-tree derivation
must reconcile both ways: a diff file without a row is an undocumented fork;
a row whose files no longer differ is stale documentation.

The commit-time `ERA_` grep also hits `drivers/sensors/cirque_pinnacle.c`.
Those tokens are Cirque's Extended Register Access (`ERA_ReadBytes`,
`ERA_WriteByte`), not an ERA firmware edit, and the file is not part of the
32-file fork surface.

## Storage-Specific QMK Surface Restored In Session 2

The following storage-driven QMK files/edits are byte-identical to
`c93ef27143` in the current working tree and therefore **do not belong in the
current fork surface**:

- `drivers/eeprom/eeprom_driver.h`;
- `drivers/eeprom/eeprom_wear_leveling.c`;
- `platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash.c`;
- `quantum/wear_leveling/wear_leveling.c` and `.h`;
- `quantum/wear_leveling/tests/rules.mk` and
  `quantum/wear_leveling/tests/wear_leveling_general.cpp`;
- `quantum/nvm/eeprom/nvm_dynamic_keymap.c` and
  `quantum/nvm/nvm_dynamic_keymap.h`;
- `quantum/nvm/eeprom/nvm_eeconfig.c`;
- `quantum/nvm/eeprom/nvm_eeprom_eeconfig_internal.h`;
- `quantum/nvm/eeprom/nvm_via.c`;
- `quantum/keyboard.h` and the storage-only `matrix_task()` visibility change
  formerly paired with it.

The stock QMK dynamic-macro RESET loop is now deliberately a dependency rather
than a fork edit. ERA NVM recognizes its sequential 16-byte
`eeprom_update_block()` transcript below QMK Core. The exact constraint and the
regression that compiles stock `nvm_dynamic_keymap.c` itself are canonical in
`era_host_peer_storage_contract.md` and `tests/era_nvm_qmk_driver`.

The retained QMK quiet-save edits are not hidden storage ownership. They remain
for one precise accepted behavior: VIA emits SAVE alongside continuous slider
SET traffic, and immediate stock persistence would write each changed slider
step durably. The ERA gate coalesces that approved stream and flushes a pending
save before controlled reset/suspend. Removing it requires an explicit product
decision to accept the extra physical writes or a cleaner equivalent
QMK-external interception; reducing the diff count alone is not a reason.

## Matrix Boundary

`era_invariants.md` remains canonical for `quantum/matrix.c` and
`quantum/matrix_common.c`. The latter is pristine. The former contains only the
documented diagnostic hooks and the accepted debounce/post-scan separation.
Do not infer permission for additional matrix-core edits from the existence of
those changes.

## Submodule Surface Outside The Five Directories

One ERA platform edit is outside the derivation above because it is a submodule
pin. `lib/chibios` and `lib/chibios-contrib` use the `custom/qmk-rp-usb` branch;
the ChibiOS pin supplies the remote-wake status hook consumed by the ERA RP2040
USB sleep synchronization path. Re-derive that with `git submodule status` and
the branch recorded in `.gitmodules`, not with the five-directory diff.

## Rebase Rule

For every rebase or QMK-core edit:

1. derive the current five-directory list from pristine `c93ef27143`;
2. reconcile every file to this ledger in both directions;
3. verify that the storage-restoration list remains absent unless a newly
   approved independent QMK reason is documented;
4. run the Source Gate and the tests in `era_performance_gates.md`;
5. update this ledger in the same change as any QMK-core movement.

During an uncommitted implementation session, do not use `... HEAD -- ...` as a
substitute for step 1: HEAD describes the previous commit, while the working
tree is the current source of truth. Once the work is committed, the two forms
must agree.
