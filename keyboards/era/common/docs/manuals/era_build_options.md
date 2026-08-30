# ERA Build Options

Genre: manual
Canonical for: where an ERA build option is declared and why that file rather
than another, every selector with its default, the dependency edges and the
layer each may be stated in, the arrangements considered and rejected, and
what a build combination is expected to do

Which source file reads an option is `era_source_map.md`'s; which board ships
which feature set is that board's `post_rules.mk`.

## Where a build option is declared

QMK divides these files by what each one can decide. Owner decision, taken on
a commissioned judgement of three candidate arrangements.

| Layer | What it can decide |
| --- | --- |
| make — ERA `.mk` fragments, a board's `rules.mk` and `post_rules.mk` | whether a translation unit compiles. `SRC`, `QUANTUM_LIB_SRC`, `CUSTOM_MATRIX`, `DEBOUNCE_TYPE`, `SPLIT_TRANSPORT`, `CONSOLE_ENABLE`, `INTROSPECTION_KEYMAP_C`, `MCU_LDSCRIPT`, `LDFLAGS`, and `$(error)` are make's alone |
| `config.h` | values and behaviour inside code compiled either way. `builddefs/common_rules.mk:275` turns every `config.h` into an `-include`; it adds no translation unit, and make never parses it |
| `keyboard.json` | what QMK's own schema owns, and only that |
| `<board>.h` | no build option. Its `#define`s are visible only to a unit that includes it. Derived macros (`ERA_TAP_DANCE_ENABLE`) are emitted from `era_tapdance_rules.mk`, never a board header |

Apply in order; the first matching rule wins.

1. **Does it change what is compiled or linked, or carry a dependency make can refuse before compiling?** → a **make variable**, `?=`-declared in the ERA fragment that reads it. Where C must also see the decision, emit a bare `-D` of the identical name from inside the same `ifeq`, and test it only with `#ifdef` / `#if defined`, never `#if X`.
2. **Is it a number, a duration or a string that only tunes code compiled either way?** → **`#ifndef` / `#define` in the C file that reads it**, with at most a guarded `ifneq ($(strip $(X)),)` pass-through so a build can override it. Make must not carry the default. **Unless every reader is a QMK core file that includes no ERA header** — then rule 2 has no reachable home and the option falls back to rule 1. `ERA_STORAGE_QUIET_DEFER_MS` is the whole of that exception.
3. **Is it a fact about one board — a pin, a PID, a product string, an EEPROM address, a keycode base?** → **that board's `config.h`**.
4. **Does QMK's schema already own the key?** → **`keyboard.json`, and nowhere else.** `rgb_matrix.led_process_limit` is the worked case: the render chunk's LED count is `RGB_MATRIX_LED_PROCESS_LIMIT`, which `data/mappings/info_config.hjson` already maps, so the boards state it in their `keyboard.json` and no ERA `.mk` or `config.h` mentions it.
5. **Is it not a choice at all, but a fact about what an ERA build *is*?** → no declaration and no default. An unconditional `-D` on the line beside the `SRC` line it is one fact with. Giving one of these a `?=` is always wrong.

**Genuinely ambiguous → rule 1.** Make is the only layer that can express the `-D`, the `SRC` line and the refusal in one place, and the only one with a command line.

**One option, one declaration**, and that is one file —
`keyboards/era/era_build_options.mk`. `era_common_qmk_rules.mk` is the sole
owner of `DEBOUNCE_TYPE`; no ERA `keyboard.json` mentions debounce.

> **REFUSED:** move the declarations into `config.h`, where a stock QMK
> keyboard puts build-time configuration.
> **WHY:** not expressible, and the arithmetic is the whole argument — the two
> fragments carry 63 `SRC +=` lines between them, 45 in
> `era_split_qmk_rules.mk` and 18 in `era_common_qmk_rules.mk`, and 27 of the
> 63 sit inside a block conditioned on a selector (13 and 14 respectively), and
> `config.h` can add no translation unit and set none of the core variables
> make owns. So the move can only take *some* of them, split by a criterion
> (does this option add a `.c` file) that is invisible to whoever reads the
> option list, and every option that moved would lose its `-e` and become an
> edit to a tracked file. Not one of QMK's own ~38 generic feature switches
> is a `config.h` define (`builddefs/generic_features.mk`).
> **REOPENS:** permanent while a header cannot add a translation unit.

> **REFUSED:** declare them in `keyboard.json`.
> **WHY:** available for half of what is needed. The schema really is open —
> `features` is a `boolean_array` with no allowlist, so `era_socd: true`
> validates and `rules_mk.py` emits `ERA_SOCD_ENABLE ?= yes` — but
> `config_h.py` contains the token `features` zero times, so the `config.h`
> half of the data-driven route does not exist; the only way to a `#define`
> is a row in upstream's `data/mappings/info_config.hjson`, which has no
> per-keyboard override. What it emits is one shape, `X_ENABLE ?= yes|no`,
> arriving at `build_keyboard.mk:143` *after* the board's own `rules.mk` and
> therefore inert against anything a board sets — and a misspelled key
> validates, emits a variable nobody reads, and fails silently. `qmk lint`'s
> `INVALID_KB_FEATURES` separately bans `tap_dance` and `via` as
> keyboard-level feature keys.
> **REOPENS:** `config_h.py` learning `features`, and the emission moving
> ahead of the board's `rules.mk`.

> **REFUSED:** adopt `post_config.h` for the derived defaults and the
> dependency checks.
> **WHY:** the argument for it — that C could then see `VIA_ENABLE` — is
> already false: GCC processes every `-D` before any `-include` and
> `builddefs/common_rules.mk` puts the `-D` set first, so an ordinary
> board `config.h` sees every make-emitted switch today. What ERA wants from
> a derived default is that it arrive bundled with the `SRC` lines it
> implies, and `post_config.h` has no build-system reach at all
> (`builddefs/build_keyboard.mk:537` only adds it to the `-include` list).
> An `#error` from it would also repeat across every in-flight compile,
> where an `$(error)` prints once.
> **REOPENS:** permanent while a header cannot add an `SRC` line.

## The three shapes the `_ENABLE` suffix still names

| Shape | Rule | Form |
| --- | --- | --- |
| selector | 1 | a make variable with a declared default a build may turn on or off, emitted as a `-D` |
| not an option | 5 | an unconditional `-D` that only looks like a selector — a fact about what an ERA split build is |
| board fact | 3 | a macro whose value is per board |

## The selectors

Every firmware selector below is declared in
`keyboards/era/era_build_options.mk`. `ERA_BUILD_VARIANT`, `ERA_SHOW_OPTIONS`
and `ERA_BUILD_IDENTITY_REPORT` are declared in
`keyboards/era/era_build_identity_options.mk` so Brick65 can adopt the same
artifact identity without an RP2040 firmware default. Each firmware `.mk`
fragment includes its declaration file first; a board assigns above that
include and wins. A selected variant's five diagnostic axes are `override :=`,
so command-line values, `MAKEFLAGS`, exported environment and `make -e` cannot
mutate the tuple.

| Selector | Default | Refused with | Meaning |
| --- | --- | --- | --- |
| `ERA_BUILD_VARIANT` | `standard` | split-only variants on a non-split target; each diagnostic selector's existing dependency refusal | board-independent identity. `standard`/`wire`/`qwin`/`cause`/`stale`/`qwin_phase` live in `common/build_variants/`; `era_build_variant_rules.mk` applies the immutable five-axis tuple. `sirind/brick65/post_rules.mk` takes the validator and printer only — its only valid identity is firmware-inert `standard`. The launcher requires make's printed tuple to match ELF witnesses |
| `ERA_BOARD_COMMON_ENABLE` | `yes` | — | the class skeleton. `era_common_qmk_rules.mk` adds the non-split unit when `SPLIT_KEYBOARD` is not `yes`, `era_split_qmk_rules.mk` the split one; both add `era_board_hooks.c`. `no` returns a board to writing those QMK hooks itself |
| `ERA_RP2040_MATRIX_ENABLE` | `yes` | — | every RP2040 board under `keyboards/era` runs the engine. Raw backend is the PIO+DMA sampler alone (`system/era_rp2040_matrix_pio.c`); `RP_DMA_REQUIRED=TRUE` is a rule-5 fact beside its `SRC` line. **There is no second raw backend and no off state** (owner decision: a CPU bit-bang scan is duplicated investment) |
| `ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY` | `128` | — | sampler settle in PIO cycles at the CPU clock. Fixed baseline (`era_performance_gates.md`); rejected downward on device evidence |
| `ERA_SOCD_ENABLE`, `ERA_KKUK_ENABLE`, `ERA_DEBOUNCE_ENABLE`, `ERA_TAPPING_CONFIG_ENABLE` | `yes` | `ERA_TAPPING_CONFIG_ENABLE=no` on a split board | — |
| `ERA_MOUSEKEY_CONFIG_ENABLE` | `yes` | `MOUSEKEY_ENABLE=no` (make); a non-default mousekey mode (`quantum/mousekey.h`) | VIA channel 13. Rule-5 fact `ERA_MOUSEKEY_RUNTIME_DELTA` beside its `SRC` line — one fact with the unit that writes the runtime variables `quantum/mousekey.c` defines |
| `TAP_DANCE_ENABLE` | `yes` | — | QMK's switch; the ERA surface is derived in `era_tapdance_rules.mk` |
| `ERA_BACKLIGHT_EFFECT_ENABLE` | `no` | `BACKLIGHT_ENABLE=no` | PWM effect layer; ids `0..3` (`era_identifier_map.md`). Off because of the ids, not the cost |
| `ERA_BACKLIGHT_ALWAYS_ON` | `no` | `BACKLIGHT_ENABLE=no`; `ERA_BACKLIGHT_EFFECT_ENABLE=yes` | indicator supply; repairs stored `enable = 0` before `backlight_init()` in `quantum/backlight/backlight.c` reads it. The pair are contradictory claims about one pin |
| `ERA_RGB_INDICATOR_ENABLE` | `no` | `RGB_MATRIX_ENABLE=no` | lock-indicator slots; ids `6..12`. Takes `rgb_matrix_indicators_kb`, `rgb_matrix_indicators_advanced_kb`, `led_update_kb` and `rgb_matrix_render_policy_kb` strongly. A board states `ERA_RGB_INDICATOR_1_LED` and optionally `_2_LED` in its `config.h` under rule 3 |
| `ERA_VIA_BOOTLOADER_ENABLE`, `ERA_EEPROM_CLEAN_ENABLE` | `yes` | — | all four combinations of the pair are valid; both off drops the ERA VIA command router |
| `ERA_STORAGE_QUIET_DEFER_MS` | `500` | — | Rule 2's stated exception: the readers that need the default are QMK core files preprocessed before any ERA header (`quantum/eeconfig.h`, `quantum/rgb_matrix/rgb_matrix.c`, `quantum/rgblight/rgblight.c`, `quantum/via.c`). Re-derive ERA readers with `git grep -l ERA_STORAGE_QUIET_DEFER_MS -- 'keyboards/era/**/*.c' 'keyboards/era/**/*.h'` |
| `RGB_MATRIX_RENDER_POLICY_ENABLE`, `RGB_MATRIX_RENDER_DOMAIN_ENABLE`, `RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE`, `RGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE` | `yes` | the three sub-options with the policy off | declared only on an `RGB_MATRIX_ENABLE` board |
| `RGB_MATRIX_IDLE_GATE_ENABLE` | `yes` | — | `rgb_matrix_task()` in `quantum/rgb_matrix/rgb_matrix.c` evaluates `SYNCING` once a millisecond (`RGB_MATRIX_IDLE_GATE_US` 1000, rule 2 in that file) instead of once a scan pass. Rule 1's tie-break; C also asks `MCU_RP`. `no` is per-pass evaluation |
| `ERA_SPLIT_SERIAL_USART_SPEED` | `460800` | a speed the SYSTEM-page labels were not written for (`era_split_via_link.c` `#error`) | the High link level; Medium/Low = /2 / /4. Wire scale = `ceil(ERA_SPLIT_SERIAL_USART_SPEED / baud)` |
| `ERA_SPLIT_EEPROM_SYNC_ENABLE` | `no` | without `VIA_ENABLE` and `RGB_MATRIX_ENABLE` | TOMAK_TKL, TOMAK79H and TOMAK79S set `yes` for any VIA keymap. `ERA_HOST_PEER_STORAGE_V1_ENABLE` is derived from it |
| `ERA_SPLIT_RP_USB_SLEEP_SYNC` | `1` | — | also accepts `RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET` |
| `ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE`, `ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE` | `no` | — | console images; `wire`/`cause`/`stale` and `qwin` set them. `ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE` is derived from either in `era_split_qmk_rules.mk` — not a selector |
| `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE` | `no` | without `ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE` | twelve segments on the qwin line. Own axis: twelve counter reads cost **4.13 µs a pass, −14.3 %**. Only `qwin_phase` sets it |
| `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE`, `ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE` | `no` | each has its preconditions refused by name | `cause` is one diagnostic selector for observations that must share a snapshot boundary |
| the seven split timing knobs | empty | — | see **The values whose default lives in C** |

The five axes are ordered `wire,qwin,phase,cause,stale`; the mapping is fixed:

| Variant | Immutable tuple |
| --- | --- |
| `standard` | `no,no,no,no,no` |
| `wire` | `yes,no,no,no,no` |
| `qwin` | `no,yes,no,no,no` |
| `cause` | `yes,no,no,yes,no` |
| `stale` | `yes,no,no,no,yes` |
| `qwin_phase` | `no,yes,yes,no,no` |

The make layer prints the resolved name and labelled tuple only when
`ERA_BUILD_IDENTITY_REPORT=yes`. The launcher derives the compiled tuple from
one link-visible production witness per axis and refuses any mismatch. The
stale axis uses a zero-storage absolute symbol in
`split/era_split_transaction_engine.c`. Direct option overrides, `MAKEFLAGS`,
exported variables and `make -e` are test inputs, not a seventh combination.

| Knob | Default | File |
| --- | --- | --- |
| `ERA_RP2040_MATRIX_PIO_ROW_RELEASE_CYCLES` | 64 | `era_rp2040_matrix_pio.c` |
| `ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES` | 4 | `era_rp2040_matrix_pio.c` |
| `ERA_SPLIT_QWIN_SEGMENT_MS` | 10000 | `diagnostics/era_split_qwin_diagnostics.c` |
| `ERA_SPLIT_QWIN_SETTLE_MS` | 0 | `diagnostics/era_split_qwin_diagnostics.c` |

| Derived | Value | File |
| --- | --- | --- |
| `ERA_HOST_PEER_STORAGE_V1_ENABLE` | derived from `ERA_SPLIT_EEPROM_SYNC_ENABLE` | `era_split_qmk_rules.mk:16-28` |
| `ERA_SPLIT_COMMUNICATION_CORE_STAGE` | `CORE1_FULL` only | `era_split_qmk_rules.mk`, beside the `$(error)` that says so |

The names in the RGB rows have no `ERA_` prefix on purpose: the macro and the
make variable are one name, and they are read in
`quantum/rgb_matrix/rgb_matrix.c`.

**A selector is in this table or it does not exist.** A selector read but never
declared still builds (`ifeq` on unset is false); a hand-copied `OPT_DEFS` line
has no default and no off state.

## The values whose default lives in C

Seven split timing knobs are rule 2: each default is a `#ifndef` in the file
that reads it; make carries an empty `?=` so a build can override.

| Knob | File |
| --- | --- |
| `ERA_SPLIT_PEER_RESPONSE_WINDOW_MS` | `era_split_wire_protocol.h` |
| `ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS` | `era_split_wire_protocol.h` |
| `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER` | `scheduler/era_split_transport_scheduler_internal.h` |
| `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS` | `scheduler/era_split_transport_scheduler_internal.h` |
| `ERA_SPLIT_SESSION_REFRESH_PERIOD_MS` | `scheduler/era_split_transport_scheduler_internal.h` |
| `ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS` | `scheduler/era_split_transport_scheduler_internal.h` |
| `ERA_SPLIT_SESSION_STALE_MS` | `scheduler/era_split_transport_scheduler_internal.h` |

## The VIA surface and its gate

**A build without VIA links no ERA VIA code** (owner decision). The gate is
`ifeq ($(strip $(VIA_ENABLE)), yes)` in `era_common_qmk_rules.mk`,
`era_tapdance_rules.mk` and `era_split_qmk_rules.mk`, and is possible only
because the ERA layer runs in the `post_rules.mk` phase.

`ERA_VIA_BOOTLOADER_ENABLE` and `ERA_EEPROM_CLEAN_ENABLE` are read in
`era_via_system.c`; their `-D` sits under the same gate. Header declarations
are deliberately ungated, so a future non-VIA caller fails at link. The
closure lives in `era_common_via.c` and each board's
`via_custom_value_command_kb()`.

## Where the options live, and how to see what a build used

| File | Its job |
| --- | --- |
| `keyboards/era/era_build_identity_options.mk` | firmware-inert `ERA_BUILD_VARIANT`, `ERA_SHOW_OPTIONS`, `ERA_BUILD_IDENTITY_REPORT`. No QMK feature switch or ERA firmware selector — Brick65 includes it without the firmware layer |
| `keyboards/era/era_build_options.mk` | **every firmware selector declaration.** The one file to edit for firmware configuration |
| `system/era_common_qmk_rules.mk` | the phase guard, and the `ifeq`/`SRC`/`-D` for the matrix engine, the feature selectors, the VIA surface, and the non-split class skeleton |
| `system/era_build_variant_rules.mk` | variant name validation, refusal of split-only variants on non-split targets, the include that applies a selected complete combination, and the machine-readable resolved identity line |
| `common/build_variants/*.mk` | the complete five-axis combination for `standard`, `wire`, `qwin`, `cause`, `stale` and `qwin_phase`; each axis `override :=` |
| `tools/era_qmk_build.sh` | the WSL-local launcher: identity line, requested/resolved match, ELF compiled axes, then names the artifact |
| `split/era_split_qmk_rules.mk` | the split `SRC` block, its `$(error)` refusals, the storage derivation and the stage |
| `features/era_tapdance_rules.mk` | the tap-dance units and the ERA surface derived from QMK's switch |
| `system/era_rgb_matrix_rules.mk` | the RGB render policy emissions and their refusal |
| `split/era_split_usb_sleep_rules.mk` | the RP USB sleep sync emission |
| `system/era_sram_resident_rules.mk` | nothing declarable — the copy-to-RAM bundle is an include, not a variable |
| `storage/era_storage_adoption_rules.mk` | nothing declarable — the EEPROM adoption bundle is an include. Its only make-visible product is the marker `era_split_qmk_rules.mk` refuses EEPROM sync without (**Storage Adoption**) |
| each board's `post_rules.mk` | **the feature set that board ships, written out explicitly** — every feature option at its value, above the includes that read them (owner decision). Diagnostics and timing axes are absent: they are per-build instruments, not board features |
| `system/era_show_options.mk` | the printer below |

Two places hold a value, and they are not two authorities. The declaration
declares with `?=`. A board `post_rules.mk` states with `=` what that board
ships, and the board wins.

**`era-build --show-options <board>:<keymap>`** prints every ERA option with
its value and origin — `file`, `command line`, or `environment`. The artifact
manifest records `requested_variant=`, `variant=`, `resolved_tuple=`,
`compiled_tuple=`, and the exact `-e ERA_BUILD_VARIANT=...` command.

The printer's set is make-declared only. It cannot silently omit a
make-declared option. **A derived set is only derived if its predicate is one
a new member satisfies by existing.** About forty rule-2 `#ifndef` values sit
outside it; `grep -rn "#ifndef ERA_" keyboards/era` enumerates them.

## The dependencies, and which layer each can be declared in

The ERA option layer runs from each board's `post_rules.mk`.
`builddefs/build_keyboard.mk` takes the keyboard `rules.mk` chain at
`builddefs/build_keyboard.mk:101-105`, generated `info_rules.mk` at
`builddefs/build_keyboard.mk:143`, the keymap's `rules.mk` at
`builddefs/build_keyboard.mk:146`, and `post_rules.mk` at
`builddefs/build_keyboard.mk:445-449` — before `INTROSPECTION_KEYMAP_C` at
`builddefs/build_keyboard.mk:473` and before `common_features.mk` /
`generic_features.mk` at `builddefs/build_keyboard.mk:494-495`. Nothing ERA
sets is consumed before that `post_rules.mk` include.

A fragment included from the wrong phase still builds and silently loses the
conditional half; `era_common_qmk_rules.mk` refuses that outright.

| Depends on | Declared where |
| --- | --- |
| split transport → `ERA_RP2040_MATRIX_ENABLE`, `NO_USB_STARTUP_CHECK=yes` | `era_split_qmk_rules.mk`, `$(error)` |
| `..._CAUSE_TIMELINE_ENABLE` → wire diagnostics + EEPROM sync + storage V1 | `era_split_qmk_rules.mk`, `$(error)` |
| `..._MIRROR_FORCE_STALE_ENABLE` → wire diagnostics | `era_split_qmk_rules.mk`, `$(error)` |
| `ERA_HOST_PEER_STORAGE_V1_ENABLE` → `ERA_SPLIT_EEPROM_SYNC_ENABLE` | `era_split_qmk_rules.mk`, derived plus `$(error)` |
| `ERA_DEBOUNCE_ENABLE` shares its runtime unit with `ERA_RP2040_MATRIX_ENABLE` | `era_common_qmk_rules.mk`, one `SRC` line each way |
| `ERA_DEBOUNCE_ENABLE` owns `DEBOUNCE_TYPE` | `era_common_qmk_rules.mk` **alone**. A board `keyboard.json` also stating `build.debounce_type: custom` is rule 4's failure — two owners, so turning the ERA selector off leaves `DEBOUNCE_TYPE = custom` standing with nothing supplying `debounce()` |
| ERA split → `ERA_TAPPING_CONFIG_ENABLE=yes` | `era_split_qmk_rules.mk`, `$(error)` beside the tap-activity `SRC` line |
| KKUK ↔ SOCD | `era_kkuk.c`, `#ifdef` with a `static inline` false. Composition, not a requirement |
| `ERA_VIA_SYSTEM_ENABLE` ← `ERA_VIA_BOOTLOADER_ENABLE` or `ERA_EEPROM_CLEAN_ENABLE` | `era_common_qmk_rules.mk`, derived; the `#ifdef` at the router entry in `era_common_via.c` is what makes all four combinations of the pair valid |
| the three common lighting selectors → the QMK feature each layers over: `ERA_BACKLIGHT_EFFECT_ENABLE` and `ERA_BACKLIGHT_ALWAYS_ON` → `BACKLIGHT_ENABLE`, `ERA_RGB_INDICATOR_ENABLE` → `RGB_MATRIX_ENABLE` | `era_common_qmk_rules.mk`, one `$(error)` each |
| `ERA_BACKLIGHT_ALWAYS_ON` ↮ `ERA_BACKLIGHT_EFFECT_ENABLE` | `era_common_qmk_rules.mk`, `$(error)`. Two claims about one pin, not a missing dependency |
| storage engine → `VIA_ENABLE` and `RGB_MATRIX_ENABLE` | `era_host_peer_storage.c`, `#error` |
| the split INPUT layer section → a one-byte `layer_state_t` | `era_split_qmk_rules.mk` supplies `LAYER_STATE_8BIT` (wire-format fact, eight layers); `era_split_peer_layer.c` `#error`s on `DYNAMIC_KEYMAP_LAYER_COUNT` above eight |
| ERA split → no `BACKLIGHT`/`RGBLIGHT`/`LED_MATRIX`/`SLEEP_LED`/`OLED`/`ST7565` | `era_split_qmk_rules.mk`, `$(error)`. `NO_USB_STARTUP_CHECK` deletes QMK's suspend loop, so the split lighting-sleep apply drives RGB Matrix only |

`tomak79s` indicator configuration changes only through a VIA write or an
EEPROM-sync reload; that is board content, not a build dependency.

## What a build combination is expected to do

**Every board builds both its `default` and its `via` keymap.** Forty-four
combinations (22 RP2040 × two keymaps), and every one passes the three
copy-to-RAM gates. `default` is the ERA performance work without the VIA
surface.

**A declared selector's off state builds.** Sole exception:
`ERA_TAPPING_CONFIG_ENABLE=no` on a split board, refused by name before any
compile. On a non-split board it turns off cleanly.

| Failure shape | What it looked like |
| --- | --- |
| unguarded cross-feature call | `era_kkuk.c` asked SOCD unconditionally |
| unit left in `SRC` under a derived selector | `ERA_EEPROM_CLEAN_ENABLE` under `ERA_VIA_SYSTEM_ENABLE` referenced its own missing statics |
| one decision with two owners | rule 4: `keyboard.json` kept `DEBOUNCE_TYPE = custom` after `ERA_DEBOUNCE_ENABLE` stopped supplying `debounce()` |
| real non-composition | `ERA_TAPPING_CONFIG_ENABLE=no` on split — the one off state that still does not build |

Four `sirind/tomak` layout-variant keymaps sit beside that set and build:
`default_ansi` (94), `default_ansi_split_bs` (95),
`default_ansi_split_rshift` (95), `default_ansi_split_rshift_bs` (96). Each
layer is the 97-key `default` filtered by that layout's `matrix` list —
re-derive, never hand-edit.
