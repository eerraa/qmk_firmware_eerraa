# ERA Build Options

Genre: manual
Canonical for: classifying a new ERA build option, deciding its owner, the
resolution order between QMK/keymap/board/ERA layers, and locating the live
selector and variant definitions

Live defaults and resolved values are source-owned. Use
`keyboards/era/era_build_options.mk`,
`keyboards/era/era_build_identity_options.mk`, the selected board's
`post_rules.mk`, and the selected variant fragment rather than copying their
values here.

## Where a build option is declared

Choose the first matching rule.

1. **Compile/link choice or a dependency make must reject before C compiles:**
   use a make variable declared with `?=` in the ERA declaration file and
   consumed by the fragment that owns the `SRC`/`OPT_DEFS`/refusal. If C
   also needs the decision, emit the same-name define from that branch.
2. **A number, duration, or string that only tunes code already being compiled:**
   keep the default beside the C reader with `#ifndef`. Make may pass through
   an explicit override but does not duplicate the default. If every reader is
   QMK core and no ERA header can reach them, use rule 1 instead;
   `ERA_STORAGE_QUIET_DEFER_MS` is the intentional case.
3. **A fact about one board:** put it in that board's `config.h`.
4. **A key QMK's schema already owns:** put it in `keyboard.json` and nowhere
   else. `RGB_MATRIX_LED_PROCESS_LIMIT` is the worked shape: QMK already maps
   its schema key to that C macro.
5. **A fact about what an ERA build is, not a user choice:** do not create a
   selector/default. Emit the derived define next to the source-selection fact
   that makes it true.

For a genuinely ambiguous build-time choice, use rule 1: make is the layer that
can keep the source selection, define, dependency refusal, and command-line
override in one decision.

Firmware selectors are declared in exactly one place:
`keyboards/era/era_build_options.mk`. Firmware-inert build identity controls
are separate in `keyboards/era/era_build_identity_options.mk`. A board's
`post_rules.mk` states the feature choices that board ships; that is a
product choice, not a second declaration.

The rejected ownership alternatives remain rejected for structural reasons:

- `config.h` cannot add/remove translation units or enforce make dependencies;
- arbitrary feature names in `keyboard.json` are not an ERA configuration
  authority merely because QMK can emit a make variable; use it only for keys
  the QMK schema actually owns. QMK lint's `INVALID_KB_FEATURES` is a separate
  guard against some unsupported keyboard-level feature names;
- `post_config.h` runs in the C include layer and cannot own source selection
  or one-shot make refusals.

These decisions reopen only if the relevant QMK layer gains the missing
build-system authority.

## The selectors

The current selector names and defaults are canonical in
`keyboards/era/era_build_options.mk`. The current identity-only controls are
canonical in `keyboards/era/era_build_identity_options.mk`. Do not maintain a
second selector/default table in this manual.

Resolution order matters:

1. QMK reads the keyboard/rules chain and generated QMK information, then the
   keymap's `rules.mk`.
2. It then reads the board's `post_rules.mk`. ERA fragments that need QMK
   switches run from this phase, so values such as `VIA_ENABLE` are already
   known.
3. Board feature assignments appear above the ERA includes that consume them.
   A declaration using `?=` therefore supplies only an otherwise-unset value;
   ordinary GNU make command-line precedence still applies to normal selectors.
4. `system/era_build_variant_rules.mk` selects one complete diagnostic
   variant from `common/build_variants/`. Those axis assignments use
   `override :=` so a named variant remains one immutable tuple.

A fragment moved earlier can still parse while silently seeing the wrong QMK
state. `system/era_common_qmk_rules.mk` owns the phase guard that rejects that
arrangement.

Every declared firmware selector's off state must build. The sole deliberate
exception is `ERA_TAPPING_CONFIG_ENABLE=no` on a split target, which the split
rules refuse by name; that same off state remains valid on a non-split target.

Derived build facts are owned beside the rule that derives them. In particular,
split dependencies and diagnostic-axis dependencies are in
`split/era_split_qmk_rules.mk`; tap-dance derivation is in
`features/era_tapdance_rules.mk`; RGB Matrix policy dependencies are in
`system/era_rgb_matrix_rules.mk`; RP USB sleep emission is in
`split/era_split_usb_sleep_rules.mk`. Timing constants such as
`RGB_MATRIX_IDLE_GATE_US` stay with their QMK C reader rather than becoming
selector defaults here. Read those files for the current predicate instead of
copying dependency truth tables here.

## The VIA surface and its gate

A build without VIA links no ERA VIA surface. The gate is the
`VIA_ENABLE` branch in `system/era_common_qmk_rules.mk`,
`features/era_tapdance_rules.mk`, and `split/era_split_qmk_rules.mk`. This
works because those ERA fragments run from `post_rules.mk` after the keymap
has stated its QMK switches.

`VIA_INSECURE` is a deliberate ERA build policy for VIA Matrix Test: when
enabled, QMK exposes debounced matrix positions to the host rather than the
upstream zero-filled reply. QMK's enabled branch materializes that state as
`-DVIA_INSECURE`; the current default belongs to the make source, not this
manual. An explicit off build remains supported.

`ERA_VIA_BOOTLOADER_ENABLE` and `ERA_EEPROM_CLEAN_ENABLE` are independent
under the VIA gate; all four on/off combinations remain supported. System VIA
routing derives their shared unit rather than coupling the two choices. Non-VIA
callers must not gain a second route merely to bypass that gate.

## Option and identity owners

| Concern | Owner |
| --- | --- |
| firmware selector declarations | `keyboards/era/era_build_options.mk` |
| firmware-inert identity controls | `keyboards/era/era_build_identity_options.mk` |
| board-shipped feature choices | each board's `post_rules.mk` |
| selector consumption and common dependency refusals | `system/era_common_qmk_rules.mk` and the feature/split fragments it includes |
| named diagnostic variants and immutable axis tuples | `system/era_build_variant_rules.mk` and `common/build_variants/` |
| resolved option printer | `system/era_show_options.mk` |
| build identity verification/artifact naming | `tools/era_qmk_build.sh` |
| copy-to-RAM adoption | `system/era_sram_resident_rules.mk`; it is a bundle, not a selector |
| storage-layout adoption | `storage/era_storage_adoption_rules.mk`; see `manuals/era_board_adoption.md` **Storage Adoption** |

Two files can therefore mention one choice without becoming two authorities:
the declaration provides an unset fallback; the board states what that product
ships.

The supported inspection command is:

    python keyboards/era/common/tools/era_build.py build <board>:<keymap> --show-options

It prints the resolved ERA options with make origin information. Build artifact
identity is checked by `tools/era_qmk_build.sh` against the make-reported
variant and the compiled witnesses; use those live outputs instead of a copied
tuple table.

When adding an option, update the declaration/consumer and any dependency
refusal in the same ownership layer. When adding a board, state only that
board's shipped choices in its `post_rules.mk`; do not fork the declaration
list into board documentation.
