# ERA Board Adoption

Genre: manual
Canonical for: the non-obvious requirements a new ERA board must satisfy: storage
layout adoption, RP2040 SRAM/boot safety, the non-split capability boundary,
lighting/VIA ownership, and board-level verification

Use `maps/era_source_map.md` to find the first implementation owner. Placement
requirements belong to `contracts/era_sram_residency_contract.md`; build-option
classification belongs to `manuals/era_build_options.md`. This manual does not
inventory current boards or copy their live option values.

## Storage Adoption

Take `storage/era_storage_adoption_rules.mk` as a bundle from `post_rules.mk`.
It force-includes `storage/era_storage_adoption.h` after the board's
`config.h`. These are schema/adoption requirements, not convenient defaults:

| # | Required precondition | Enforcement |
| --- | --- | --- |
| 1 | `TOTAL_EEPROM_BYTE_COUNT == 24576`: QMK's `WEAR_LEVELING_LOGICAL_SIZE` is 24 KiB, with twice that backing | `split/era_host_peer_storage.c` span assertion |
| 2 | `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` is exactly 16 KiB | shared core0/core1 image equality |
| 3 | `EECONFIG_SIZE == 37`; the board declares no `EECONFIG_KB_DATA_SIZE` or `EECONFIG_USER_DATA_SIZE` | adoption header refusal plus the `ERA_EEPROM_CONFIG_ADDR == 37U` assertion |
| 4 | `VIA_EEPROM_MAGIC_ADDR == ERA_EEPROM_CONFIG_END`, with the equivalent non-VIA dynamic-keymap base | layout/keymap placement assertions |
| 5 | the board config struct fits `ERA_EEPROM_KEYBOARD_CONFIG_SIZE` (8 bytes) | a board-adjacent `_Static_assert`; `sirind/common/tomak_era_keyboard_config.h` is the reference shape |

The bundle is deliberately an include rather than a set of variables:
`VIA_EEPROM_MAGIC_ADDR` depends on the shared layout header even in QMK units
that include no ERA header, and `split/era_split_qmk_rules.mk` refuses EEPROM
sync when the bundle marker is absent. Do not gate the bundle on the sync
selector; VIA and non-VIA keymaps for one board must keep one EEPROM geometry.

Product-specific records may occupy an already-reserved ERA seat only if their
local static assertions keep them inside the existing schema. Such use does not
change the protected RESET KEY range, State Sync format, split wire/storage
schema, or become a template for another board.

## Non-Split Capability

`ERA_RP2040_MATRIX_ENABLE` is eligible only for an RP2040, `COL2ROW` board
that does not provide another matrix source. `system/era_rp2040_matrix_pio.c`
and `system/era_common_qmk_rules.mk` own the live compile refusals and derived
QMK settings. Split transport requires this engine; the engine itself must
remain usable without the split layer.

A board that has not previously scanned through this engine owes device
verification. The settle/release evidence and pass bands are owned by
`manuals/era_performance_gates.md`; a successful build does not prove the
electrical matrix settles correctly.

## Copy-To-RAM Policy

Every ERA RP2040 board runs from SRAM. The permanent architecture exception is
`sirind/brick65`, which is ATmega32U4 and cannot adopt the RP2040 residency
layer. RP2040 boards include `system/era_sram_resident_rules.mk`; the linker
layout, marker, pre-copy window and vector defaults are owned there and by
`contracts/era_sram_residency_contract.md`.

The residency marker is also a flash-safety premise: the ERA RP2040 NVM backend
may program/erase flash only while firmware execution and both vector tables are
resident in SRAM. A bank rotation may hold core0 through finite construction;
its width is device-measurement evidence, not a placement assertion.

The pre-copy double-tap arm is inseparable from the residency bundle. The
linker assertions must prove that the strong pre-copy hook and bootloader magic
are present before `RP2040_BOOTLOADER_DOUBLE_TAP_RESET_PRE_COPY_ARM` is useful.
The exact arm/close window is canonical in
`contracts/era_sram_residency_contract.md`.

Vector defaults follow each driver's own presence condition. Do not claim the
PIO, PWM, or DMA vector slots unconditionally; `system/era_vector_defaults.c`
owns those conditions and `common/tools/era_residency_gate.sh` checks the
linked `.vectors` result.

## Non-Split Board Baseline

A non-split RP2040 board must be able to use the common matrix engine,
copy-to-RAM image, pre-copy double-tap handling, and the common ERA VIA feature
surface without adopting split transport. The matrix sampler owns PIO1 and two
DMA channels; a board that needs PIO1 for another purpose is outside this
backend's supported hardware boundary.

The common VIA units are selected under
`manuals/era_build_options.md` **The VIA surface and its gate**. SYNC remains
a split-only surface.

Split and non-split USB suspend paths are intentionally different. A split
build removes QMK's stock suspend loop through `NO_USB_STARTUP_CHECK` and
`split/era_split_keyboard.c` owns the replacement. A non-split build keeps
QMK's loop and adds the common frame-loss bridge in
`system/era_usb_session.c`. Every RGB-capable ERA board must compile the
matching QMK sleep capability in `keyboard.json`
(`rgb_matrix.sleep: true` or `rgblight.sleep: true`).

Automatic RGB sleep uses both explicit USB suspend and observed USB-frame loss.
Frame absence is evidence only while observation is fresh; an unobserved span
must not be reclassified as a stationary counter. `USB->SOFRD` has a
read-with-side-effect remote-wake role, so `system/era_usb_session.c` must
respect ChibiOS ownership of that read instead of consuming it opportunistically.
A SETUP packet may be the first activity after suspend with neither
`DEV_RESUME_FROM_HOST` nor SOF; the ChibiOS submodule wake seam is therefore part
of this boundary. The policy owner for split lighting sleep is
`contracts/era_authority_contract.md` **Lighting Sleep Ownership**.

**Powered-hub host death reaching `USB_SUSPENDED` remains unmeasured.** That
device measurement is still owed; do not turn the absence of evidence into a
PASS.

**The lighting-surface rule:** use QMK's own lighting channel when it already
owns the concept. A family-wide extension belongs in the common feature/router
surface; a one-product extension belongs behind the weak board hooks in
`system/era_board_hooks.h`. Use `maps/era_identifier_map.md` before
allocating or reusing an identifier. Do not infer support from sibling VIA
definitions; compare a definition with the handler that answers it.

Continuous lighting controls join the persistence gate that owns their record;
discrete toggles keep their discrete persistence path. For a board-local
continuous keyboard-channel control, the save path joins
`era_common_via_keyboard_channel_save()` in `system/era_common_via.c` rather
than inventing a second quiet-save mechanism.

## The Adoption Checklist

A new board provides:

- `keyboard.json` for QMK-owned matrix, pins, layouts and lighting capability;
- `config.h` only for board facts, following `era_build_options.md` rules 2-4;
- `post_rules.mk` with that board's explicit shipped feature choices above the
  ERA includes that read them; EEPROM-sync boards take **Storage Adoption**;
- both `default` and `via` keymaps for every RP2040 ERA board;
- an owner-authored `*-VIA.json` beside the VIA keymap. It is product content,
  not compiled firmware; keep the local Draft Definition usevia.app-compatible
  and put app-only controls in the app-owned definition instead;
- a board `.c`/`.h` only for product-specific behavior or names that the
  common class and weak-hook surfaces do not own.

The local Draft Definition is paired with the app-owned
`the-via-eerraa/era-definitions/custom/v3` source; the `FEATURE_COVERAGE`
fixture in `the-via-eerraa/tests/era-definition.test.ts` exercises the
cross-repository feature mapping. Official upstream definition ownership is `the-via/keyboards/v3`. These are
source/test anchors, not an inventory to mirror here.

The class skeletons are `system/era_nonsplit_board.c` and
`split/era_split_board.c`; board extensions use `system/era_board_hooks.h`.
Turning `ERA_BOARD_COMMON_ENABLE` off makes the board responsible for the
class hooks again, including continued servicing of the double-tap window.

Verification is scoped to the adopted boundary. Run the supported launcher for
each keymap offered as evidence and the residency/vector checks required by
`manuals/era_performance_gates.md`. For the known non-split decode path, the
existing direct smoke command remains:

    era-build era/newone/odessey60s:via

A successful non-split smoke must not link peer/HOST-PEER symbols.
