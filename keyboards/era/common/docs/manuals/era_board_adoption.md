# ERA Board Adoption

Genre: manual
Canonical for: what a new ERA board needs, whole — the copy-to-RAM policy and
what its marker changes beyond placement, the non-split capability boundary
and baseline, the storage adoption bundle and its five preconditions, and the
adoption checklist

Per-file source ownership is `era_source_map.md`'s, placement
`era_sram_residency_contract.md`'s, option declarations `era_build_options.md`'s.

## Storage Adoption

`storage/era_storage_adoption_rules.mk` is the bundle, included from
`post_rules.mk` above the ERA split fragment. It puts
`storage/era_storage_adoption.h` on `CONFIG_H`, force-included after the
board's `config.h`.

| # | Precondition | Supplied? | What catches it |
| --- | --- | --- | --- |
| 1 | `TOTAL_EEPROM_BYTE_COUNT == 24576` (`WEAR_LEVELING_LOGICAL_SIZE` 24 KiB, backing twice that) | yes | `era_host_peer_storage.c`'s span assert |
| 2 | `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` **exactly** 16 KiB | yes | an *equality* against the shared core0/core1 image, so "large enough" is not the requirement and QMK's own computed default does not satisfy it |
| 3 | `EECONFIG_SIZE == 37` — the board declares no `EECONFIG_KB_DATA_SIZE`/`EECONFIG_USER_DATA_SIZE` | no, refused | the header's own `#error`, and behind it the `ERA_EEPROM_CONFIG_ADDR == 37U` assert, which is the one that still fires when a *keymap* `config.h` sets it |
| 4 | `VIA_EEPROM_MAGIC_ADDR == ERA_EEPROM_CONFIG_END`, and the non-VIA build's `DYNAMIC_KEYMAP_EEPROM_ADDR` equivalent | yes | the layout-options-at-296 and keymap-at-297 asserts |
| 5 | the board's own config struct fits `ERA_EEPROM_KEYBOARD_CONFIG_SIZE` (8 bytes) | no, named | a `_Static_assert` beside the struct — `sirind/common/tomak_era_keyboard_config.h` is the shape to copy |

`comm/riley` is a non-split exception to using that eight-byte board seat as a
standalone record: its three RGBLight indicator HSV values need ten bytes after
the three 2-bit modes, Indicator-Only bit and validity bit are packed. Riley
therefore owns the existing eight-byte `ERA_EEPROM_RGB_INDICATOR_CONFIG` seat
plus the first two bytes of its board seat as one ten-byte record. Static
asserts in `comm/riley/riley_common.c` keep the record below
`ERA_EEPROM_SYNCABLE_RESERVED_OFFSET`. The reserve size, protected RESET KEY
range, ERA_CONFIG domain width, State Sync format and split storage/wire schema
do not change; this one-product use is not a new split-adoption template.

**An include and not variables.** The parts fail as a set; `VIA_EEPROM_MAGIC_ADDR`
takes a macro from `era_eeprom_layout.h` that QMK units read with no ERA header.
`era_split_qmk_rules.mk` declines `ERA_SPLIT_EEPROM_SYNC_ENABLE = yes` on a
board that has not taken the bundle, before any compile.

**It is deliberately not gated on the sync selector.** The stored layout is a
board fact; gating it would give a non-VIA keymap a different EEPROM geometry
from its VIA keymap.

## Non-Split Capability

A board is eligible for `ERA_RP2040_MATRIX_ENABLE = yes` when it is RP2040,
`COL2ROW`, and carries no matrix source of its own — `era_rp2040_matrix_pio.c`
rejects the other two. Setting it implies `CUSTOM_MATRIX = yes`. It is the
manifest default (`ERA_RP2040_MATRIX_ENABLE ?= yes`); every board under
`keyboards/era` runs the engine in `default` and `via`.

**The engine does not carry the split.** Split transport requires the engine;
the engine requires nothing of split (`era_split_qmk_rules.mk`).

A board that has never scanned a key through this engine owes device
verification. The 128-cycle `ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY` 128 is
tuning measured on TOMAK79H only; every other board carries that number
without a measurement of its own. A slower-settling matrix produces a missed
or ghosted key, not a build failure.

## Copy-To-RAM Policy

**Every ERA RP2040 board runs the image from SRAM. XIP is retired** (owner
decision). Residency is the default, not a capability a board adopts. **All
twenty-three RP2040 boards under `keyboards/era` are on it.** `sirind/brick65`
is atmega32u4 (2.5 KB SRAM) and a permanent exception, not a debt: it takes
none of the ERA firmware layer. Its `post_rules.mk` includes only the common
make-time validator and option printer.

`system/era_sram_resident_rules.mk` is the include (linker script, marker, map,
pre-copy window, vector defaults). A non-split board links and lays out
correctly under it. `common/tools/era_residency_gate.sh` measures every board
and keymap; its pass bands are `era_performance_gates.md`'s.

**What the marker changes beyond placement is the flash-safety premise.** ERA
NVM's RP2040 backend issues program/erase while the firmware and both vector
tables execute from SRAM. Housekeeping returns after at most one 4-KiB sector;
a mandatory bank rotation may still hold core0 until its finite construction
completes. That width is a device-measurement obligation, not a placement claim.

**The pre-copy arm is not separable from the bundle.** `era_boot_core1_halt.c`
would compile on an XIP board, but two linker checks make it safe: the
`early_hardware_init_pre` `ASSERT`, and the `.era_bootloader_magic` size
assertions. Without the first, a wrong `SRC` leaves QMK's weak stub linked
with `PRE_COPY_ARM` still defined, and `__late_init()`
(`platforms/chibios/bootloaders/rp2040.c`) then never arms: **double tap
stops working permanently and silently**. That is why the bundle is an
include and not a variable.

The window itself is `era_sram_residency_contract.md`'s: non-blocking, only
an eligible physical RUN arms, ROSC stable-release guard 15–102 ms.

| Condition | What catches it |
| --- | --- |
| `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM` without `..._NONBLOCKING` | does not compile (`double_tap_armed` is declared only under the second) |
| both without a window closer | bricks: every physical RUN enters BOOTSEL. The only closer caller is `era_common_features.c`; the class skeleton supplies `housekeeping_task_kb` (**The Adoption Checklist**) |
| missing `era_common_qmk_rules.mk` | one `$(error)` in `system/era_sram_resident_rules.mk` names both files before any compile |
| no `RP2040_BOOTLOADER_DOUBLE_TAP_RESET` | two linker `ASSERT`s (`magic_location` missing). Every ERA RP2040 board sets `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT` 1000U |

**Five vector slots are conditional, and each condition is the driver's own.**
Answer a finding with the driver's condition, never by deleting the entry.
Run the `.vectors` gate when adopting — `era_residency_gate.sh` reads what was
actually linked, and it has caught this class three times.

| Slot | Condition | Why the unconditional form is wrong |
| --- | --- | --- |
| `Vector5C`/`Vector64` (PIO0_IRQ_0, PIO1_IRQ_0) | whichever slot the ERA wire backend leaves, chosen by `SERIAL_PIO_USE_PIO1`; both when no backend is linked | *a* backend claims one PIO slot, not *the* backend — omitting `Vector5C` assumes the wrong one, and an unconditional `Vector64` duplicates the moment a board sets the selector |
| `Vector50` (PWM_IRQ_WRAP) | `HAL_USE_PWM` with any `RP_PWM_USE_PWMn` | the ChibiOS PWM LLD installs it under exactly that, so claiming it unconditionally is a duplicate symbol on a board with QMK `backlight` on hardware PWM |
| `Vector6C`/`Vector70` (DMA_IRQ_0/1) | `RP_DMA_REQUIRED`, which `RP2040.mk` defines for `WS2812_DRIVER=vendor` and `hal_spi_lld.h` for a board using SPI | omitting them assumes every ERA image installs them for RGB — a board with no addressable LED leaves both slots in the flash carve-out |

Reading those slots needs `hal.h`, which `era_vector_defaults.c` includes for
exactly this. Getting a condition wrong fails the link or the gate, never the boot.

## Non-Split Board Baseline

A non-split ERA board gets the whole ERA VIA custom UI, the copy-to-RAM
image, the pre-copy double-tap arm, and the matrix engine. **No shared-code
gap remains** — adoption is per-board content and device verification.

Every RP2040 board samples through the PIO+DMA backend, the engine's only
one — why there is no second is `era_build_options.md`'s. Pins come from that
board's `keyboard.json`; the frame is padded to the next power of two of the
row count. The sampler claims PIO1 whole and two DMA channels; own PIO1 use
is the one unsupported case, and no ERA board does.

The VIA surface needs no split-specific change: `era_common_qmk_rules.mk` adds
every feature's VIA unit for any VIA keymap. Gate:
`era_build_options.md`'s **The VIA surface and its gate**. Only
`era_split_via_sync.c` is split-gated. **SYNC is split-only.**

| Kind | Suspend loop |
| --- | --- |
| split | `NO_USB_STARTUP_CHECK = yes` — `era_split_qmk_rules.mk` refuses anything else, so a PEER half keeps `keyboard_task()` running. That switch **deletes QMK's own suspend loop** (`tmk_core/protocol/chibios/chibios.c`); `era_split_keyboard.c` replaces it |
| non-split | stock loop: `USB_DRIVER.state == USB_SUSPENDED` drives `suspend_power_down_quantum()`. Verified in cflags: `era/linx3/n86:via` carries neither `NO_USB_STARTUP_CHECK` nor `RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET`, and links `suspend_power_down` and `rgb_matrix_set_suspend_state` |

**The sleep decision takes two detectors on every ERA board** (owner
decision): explicit USB suspend **and** loss of USB frames.
`system/era_usb_session.c` is common; the split layer ORs
`era_usb_session_frames_lost()` into its predicate, a non-split board gets
the arm beside QMK's loop, and both drive the same two `suspend_*_quantum()`
entry points.

On a non-split board with `ERA_BACKLIGHT_EFFECT_ENABLE`, those same suspend
hooks also retire any active Pulse one-shot and stop QMK's continuous Breathing
virtual timer before the generic suspend path drives the PWM rail to zero.
`features/era_backlight.c` owns that effect state; `system/era_usb_session.c`
owns where it joins the non-split suspend/wake path. This is necessary because
frame-loss sleep can leave `keyboard_task()` running, and QMK's backlight
Breathing callback is itself a virtual-timer ISR.

The TOMAK split family adds one independent presentation policy on top of those
two USB facts: `last_matrix_activity_elapsed()` against a persisted 1..65535
second RGB idle timeout. Its eight-byte board record had two reserved zero bytes;
they now carry the uint16 timeout, with legacy zero normalized to the
600-second / 10-minute default. The record is in the portable ERA_CONFIG domain, so EEPROM
sync normally converges the setting. Runtime ownership does not: each DUAL-HOST
half evaluates its own value/activity, while a HOST-PEER PEER consumes only the
HOST's wire sleep fact (`split/era_split_keyboard.c`,
`sirind/common/tomak_era_keyboard_config.h`).

> **REFUSED:** routing the split apply through `suspend_power_down_quantum()`.
> **WHY:** it would credit the render-policy flush for a frame it paints black.
> **REOPENS:** the flush no longer treats a painted-black frame as a completed policy apply.

**The two arms are one physical event seen twice, not two USB signals.** A
host suspends by stopping SOF; the controller latches SUSPEND after 3 ms of
idle bus. The first arm reads the ChibiOS state machine, the second the
frame-counter register. They diverge where that machine has been taken out of
`USB_SUSPENDED` by control traffic while the bus is still dead, and where the
ERA remap rewrites the state.

**The frame arm counts only observed absence:** the age verdict requires the
last register read to be fresh; a sample after an unobserved span treats an
equal counter as unknown, not stillness. Device-reported `slp` == `brkms`
after a starved flash window grounds the clause. One fresh sample re-validates
either verdict. One deliberate observation gap is different: while remote wake
has armed `USB_INTE_DEV_SOF`, ChibiOS owns the side-effect `SOFRD` read. The ERA
sampler tracks that ownership separately; a pending `DEV_SOF` means a frame has
arrived, while an owned interval with no pending SOF for the full 300 ms is
observed frame loss. This prevents the ISR stand-off from being mistaken for the
flash-starvation case and returning not-lost forever (`system/era_usb_session.c`).

**`USB->SOFRD` is read-with-side-effect.** The RP2040 clears `DEV_SOF` on
read, and that flag is load-bearing for `usb_lld_wakeup_host()`, which arms
`USB_INTE_DEV_SOF` so `_usb_wakeup()` runs. `era_usb_session.c` stands off
while that enable bit is set and rate-limits the read to 1 kHz.

**`rgb_matrix_set_suspend_state()` has one owner on a split board.** The rule
is `era_authority_contract.md`'s **Lighting Sleep Ownership**; this row owns
where it lives. The resolver in `era_split_keyboard.c` reconciles the gate to
the current fact on every refresh; only wire publication and diagnostics are
edge-triggered. `era_host_peer_response.c` publishes the wire fact;
`era_host_peer_source_snapshot.c` captures the resolver's decision. TOMAK
STATUS/lock-indicator presentation never clears suspend; QMK's normal wake may
clear it and the next resolver refresh reasserts any still-live SOF/idle reason.

| ChibiOS delta | Scope |
| --- | --- |
| SETUP-packet wake in `hal_usb_lld.c` | **unconditional** — every board in this fork. RP2040 can present SETUP as first activity after suspend, with no `DEV_RESUME_FROM_HOST` and no SOF |
| `RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET` | split-only (`split/era_split_usb_sleep_rules.mk`). A non-split board reaches the same outcome outside ChibiOS: QMK's loop exits with the bus still dead; one pass later the frame-loss arm puts lighting out again |

**Powered-hub host death reaching `USB_SUSPENDED` is unmeasured** on any
board; that measurement is owed, and this paragraph is the only place it is
written down.

Hardware families from the 25 `keyboard.json` files. VIA protocol channels
are `quantum/via.h` (`id_qmk_backlight_channel` 1, `id_qmk_rgblight_channel`
2, `id_qmk_rgb_matrix_channel` 3). JSON homes are the 28 `*-VIA.json` files
beside each `via` keymap, identical in lighting commands and `qmk_*` string
aliases to `the-via-eerraa/era-definitions/custom/v3` (peer extra: five H7S
definitions, not QMK boards).

| Family | Boards | Protocol | JSON home |
| --- | --- | --- | --- |
| RGB Matrix | 10 | channel 3, `id_qmk_rgb_matrix_*` | expanded channel-3 commands: `sirind/tomak` (six half files), `sirind/chickpad`, `linx3/fave65s`. String alias `"qmk_rgb_matrix"`: `sirind/brick65`, `linx3/n86`, `linx3/n87`, `sirind/klein_sd`. Indicator LEDs only, no RGB-matrix menu: `sirind/brick65s` |
| PWM backlight | 12 | channel 1, `id_qmk_backlight_*` | none of the 28 files contain `id_qmk_backlight_*`. Nine effect boards use keyboard channel 0, ids `0..3` (`id_custom_backlight_*`, `id_custom_breathing_period`, `id_custom_blink_speed`); the string name of id 3 is legacy, its UI meaning is Pulse Speed |
| RGBLIGHT | 7 | channel 2, `id_qmk_rgblight_*` | channel 2 `id_qmk_rgblight_*` on `newone/odessey60s`, `newone/odessey60h`, `comm/classicd_a1_ug`, `comm/classicd_core`, `comm/classicd_coreless`, `comm/7b75`, `comm/riley`. 7B75 is physically a top badge. Riley keeps all three LEDs in the RGBLight effect range and overlays per-LED lock policy with `RGBLIGHT_LAYERS` |

Two-family: `sirind/klein_sd` (backlight + per-key matrix),
`comm/classicd_a1_ug` / `classicd_core` / `classicd_coreless` (backlight +
underglow), and `comm/7b75` (indicator-supply backlight + RGB badge).
`newone/h1` is in none. Family conversion is an owner decision.

**The ERA RGB improvements are RGB_MATRIX-only**
(`RGB_MATRIX_RENDER_POLICY`, independent indicators, deferred config flush).

Three boards take no PWM-backlight VIA menu: `divine`, `sirind/klein_hs` and
`sirind/klein_sd` (owner decision 2026-08-18). **Answering no is more than
deleting the menu:** `ERA_BACKLIGHT_LOCK_ENABLE` repairs a stored disabled or
zero-level block before `backlight_init()` reads it and refuses QMK backlight
keycodes that would persist the rail below level 1. Brightness is
`BACKLIGHT_DEFAULT_LEVEL` in that board's `config.h`. `klein_sd` still ships
`"qmk_rgb_matrix"`. `comm/7b75` deliberately combines that same lock policy
with the common effect menu: id 0 is labelled `Indicator Brightness` and ranges
1..10, while Pulse and USB sleep may still drive the live PWM duty to zero.

**The lighting-surface rule:** a lighting surface past VIA's own channel
(RGB-matrix 3 / RGBLIGHT 2 / backlight 1) goes one of two homes. The common
router in `system/era_common_via.c` runs first, so ids `0..3` are the
backlight feature's wherever `ERA_BACKLIGHT_EFFECT_ENABLE` is on.

| Home | When | Files / ids |
| --- | --- | --- |
| common feature | family behaviour | `features/era_backlight_via.h` ids `0..3` behind `ERA_BACKLIGHT_EFFECT_ENABLE` (nine boards: `era65`, `linx3/n8x`, `newone/a1`, `comm/et_tkl`, `comm/classicd_a1`, `comm/classicd_a1_ug`, `comm/classicd_core`, `comm/classicd_coreless`, `comm/7b75`). `features/era_rgb_indicator_via.h` ids `6..12` behind `ERA_RGB_INDICATOR_ENABLE`: `linx3/n86` and `linx3/n87` answer `6..12`; `sirind/brick65s` answers `7..12`; `linx3/fave65s` answers `7..9` |
| board hooks | one product | weak `era_board_via_get_value()` / `era_board_via_set_value()` in `system/era_board_hooks.c`. Overriders: `sirind/common/tomak_common.c` ids `0..4`; `newone/common/odessey_common.c` ids `1..4`; `comm/riley/riley_common.c` ids `13..23` |

A continuous lighting control's persistence is deferred by the gate that owns
its save event. Four parts: the number is `ERA_STORAGE_QUIET_DEFER_MS` 500 and
there is no second one; the arm is the VIA save and never the set (`_noeeprom`
on every setter); no maximum-age flush beside the quiet timer; **one
persistence range has one gate**.

| Channel | Gate |
| --- | --- |
| VIA RGB-matrix | `quantum/eeconfig.h` helper, instantiated in `quantum/rgb_matrix/rgb_matrix.c` |
| VIA RGBLIGHT | `quantum/rgblight/rgblight.c` |
| keyboard | `system/era_board_hooks.c`; a claimant joins `era_common_via_keyboard_channel_save()` in `system/era_common_via.c` |

A discrete control keeps its immediate write (tap dance, SOCD, debounce,
tapping, mousekey, NKRO toggle). A discrete control that shares a config
record with a continuous one rides that record's gate.

| Set-time toggle | Site |
| --- | --- |
| NKRO bit | `features/era_nkro_via.c`, QMK's `eeconfig_update_keymap()` |
| odessey/Riley Velocikey | `newone/common/odessey_common.c` / `comm/riley/riley_common.c`, `rgblight_velocikey_toggle()` |

Both compare first and write nothing when the bit already holds. Exceptions
to the continuous-control arm, not a counter-example to it.

A common lighting unit takes QMK's weak render hooks strongly:
`era_rgb_indicator.c` defines `rgb_matrix_indicators_kb`,
`rgb_matrix_indicators_advanced_kb`, `led_update_kb` and
`rgb_matrix_render_policy_kb`.

Riley is deliberately not that RGB Matrix unit. `comm/riley/riley_common.c`
installs three mutable one-LED `RGBLIGHT_LAYERS`; QMK renders the ordinary
RGBLight effect first and those layers override only the selected lock LEDs in
the same flush. No scan/housekeeping render loop and no QMK-core
`rgblight_indicators_kb` hook are added. Because
`RGBLIGHT_LAYERS_OVERRIDE_RGB_OFF` is absent, USB suspend, RGB sleep and host
loss still darken every layer. GP25 remains QMK's independent Caps-lock LED.

**Check a migrated definition against a handler, not against its siblings.**
Every declared piece of lighting hardware owes a surface or a decision that it
has none, and every menu entry owes a handler.

## The Adoption Checklist

What a new ERA board needs, whole:

- a `keyboard.json` — pins, matrix, LED positions, layouts, the lighting engine
- a `config.h` — the board facts of `era_build_options.md`'s rule 3: the
  tap-dance keycode base, the double-tap reset settings, any USB identity
- a `post_rules.mk` — the feature set written out explicitly, ERA includes
  under it, and for EEPROM sync the storage bundle (**Storage Adoption**)
- a `via` keymap
- the owner-authored `*-VIA.json` definition (below); not compiled
- **optionally** a board `.c` for genuine product behaviour, and **optionally**
  a board `.h` naming tap-dance slots for a source keymap
- the residency gate on both keymap builds, the `.vectors` gate, and the
  device first-run

**A board `.c` is not required.** The class skeleton owns the QMK hooks every
board of a class would otherwise wire identically. Product-specific units such
as Riley extend only the weak board-hook surface; they do not duplicate the
class skeleton's six QMK entry points.

| Class | File | Six hooks |
| --- | --- | --- |
| non-split | `system/era_nonsplit_board.c` | `housekeeping_task_kb`, `matrix_init_kb`, `eeconfig_init_kb`, `process_record_kb`, `via_init_kb`, `via_custom_value_command_kb` |
| split | `split/era_split_board.c` | `housekeeping_task_kb`, `keyboard_pre_init_kb`, `keyboard_post_init_kb`, `suspend_wakeup_init_kb`, `process_record_kb`, `via_custom_value_command_kb` |

A board extends them through the weak hook set in `system/era_board_hooks.h`.

**That is what retires the double-tap closer hazard.**
`era_common_features_task()` in `system/era_common_features.c` is the only
caller of `rp2040_bootloader_double_tap_reset_task()`. A board that sets
`ERA_BOARD_COMMON_ENABLE = no` writes its class's six hooks itself and
re-acquires the hazard.

**A `via` keymap does not bring the VIA definition JSON.** That file is
owner-authored product content and is not compiled. The app's bundled ERA
source is `the-via-eerraa/era-definitions/custom/v3`; official ownership is
`the-via/keyboards/v3`. QMK-local `*-VIA.json` files are usevia.app-compatible
Draft Definitions: do not add Custom VIA-only controls. **Every board has a
local copy** — a split board has two, one per half — as `<BOARD>-VIA.json`
beside the `via` keymap (28 = 25 + 3). Lighting commands and `qmk_*` string
aliases match the 28 peer v3 files of the same names. `FEATURE_COVERAGE` in
`the-via-eerraa/tests/era-definition.test.ts` matches those 28 for
`id_qmk_mousekey`, `id_qmk_custom_nkro`, `id_qmk_split_link` and
`id_qmk_eeprom_sync` (`sirind/brick65` is in none: stock VIA, RGB-matrix
alias only). The Draft/app delta is tap-dance terms (QMK legacy ids
`36,41,…,71` and channel 15 id 1; peer exact-ms ids `72..79` and channel 15
id 5), not lighting.

Re-prove the non-split path after any change to the guard boundary:

    era-build era/newone/odessey60s:via

Check the decode loop against the split build, and that `arm-none-eabi-nm`
links no peer/host-peer symbol.
