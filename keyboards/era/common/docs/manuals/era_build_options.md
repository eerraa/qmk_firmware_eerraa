# ERA Build Options

Genre: manual
Canonical for: where an ERA build option is declared and why that file rather
than another, every selector with its default, the dependency edges and the
layer each may be stated in, the arrangements considered and rejected, and
what a build combination is expected to do

Which source file reads an option is `era_source_map.md`'s; which board ships
which feature set is that board's `post_rules.mk`.

## Where a build option is declared

**QMK divides these files by what each one is *able* to decide, and that
division — not taste — decides where an ERA option goes.** Owner decision, taken
on a commissioned judgement of three candidate arrangements; what was rejected
and why is at the end of this section, so that a session applies the rule
instead of re-deciding it.

- **make** — the ERA `.mk` fragments, a board's `rules.mk` and its
  `post_rules.mk` — is the only layer that can decline to compile a translation
  unit. `SRC`, `QUANTUM_LIB_SRC`, `CUSTOM_MATRIX`, `DEBOUNCE_TYPE`,
  `SPLIT_TRANSPORT`, `CONSOLE_ENABLE`, `INTROSPECTION_KEYMAP_C`,
  `MCU_LDSCRIPT` and `LDFLAGS` are make's alone, and so is `$(error)` — the one
  refusal that fires once, before any compile, naming its own file.
- **`config.h`** decides values and behaviour inside code that is compiled
  either way. `builddefs/common_rules.mk:275` turns every `config.h` in the
  chain into an `-include` flag, so a `#define` there reaches every translation
  unit exactly as a `-D` does. It can add no translation unit, and make never
  parses its contents.
- **`keyboard.json`** declares what QMK's own schema owns, and only that.
- **`<board>.h` declares no build option at all.** It is the one file in the
  chain whose `#define`s are visible only to a unit that includes it, and that
  is a defect generator rather than a nuance — see below.

Apply in order; the first rule that matches wins.

1. **Does it change what is compiled or linked, or carry a dependency make can
   refuse before compiling?** → a **make variable**, `?=`-declared in the ERA
   fragment that reads it. Where C must also see the decision, emit a bare `-D`
   of the identical name from inside the same `ifeq`, and test it only with
   `#ifdef` / `#if defined`, never `#if X`.
2. **Is it a number, a duration or a string that only tunes code compiled
   either way?** → **`#ifndef` / `#define` in the C file that reads it**, with
   at most a guarded `ifneq ($(strip $(X)),)` pass-through in make so a build
   can override it. Make must not carry the default: a default split from its
   reader makes the reader unreadable as documentation. **Unless every reader
   is a QMK core file that includes no ERA header** — then rule 2 has no
   reachable home and the option falls back to rule 1.
   `ERA_STORAGE_QUIET_DEFER_MS` is the whole of that exception, and it is why
   the exception is stated rather than left as a judgement call.
3. **Is it a fact about one board — a pin, a PID, a product string, an EEPROM
   address, a keycode base?** → **that board's `config.h`**.
4. **Does QMK's schema already own the key?** → **`keyboard.json`, and nowhere
   else.** `rgb_matrix.led_process_limit` is the worked case: the render
   chunk's LED count is `RGB_MATRIX_LED_PROCESS_LIMIT`, which
   `data/mappings/info_config.hjson` already maps, so the boards state it in
   their `keyboard.json` and no ERA `.mk` or `config.h` mentions it. Ten ERA
   boards run RGB Matrix and nine of them carry the key: `sirind/brick65` is
   the atmega32u4 exception and states none of it. A build still overrides it, because the generated
   `info_config.h` wraps every schema value in `#ifndef` and GCC processes each
   `-D` first, so a build-time override changes one without a second declaration
   site — which is what let the in-place batch bisect its own chunk size from a
   rung rather than by editing nine board files twice.
5. **Is it not a choice at all, but a fact about what an ERA build *is*?** → no
   declaration and no default. An unconditional `-D` on the line beside the
   `SRC` line it is one fact with, so setting one without the other fails the
   link rather than resolving to nothing. Giving one of these a `?=` is the one
   move that is always wrong.

**Genuinely ambiguous → rule 1**, stated as a tie-break with a reason so it
does not become a judgement call. Make is the only layer that can express the
whole option in one place — the `-D`, the `SRC` line and the refusal — and the
only one with a command line. An option in the wrong make file is inconvenient;
an option in `config.h` that should have been a selector cannot be set at all
without editing a tracked file.

**One option, one declaration**, and that is one *file* --
`keyboards/era/era_build_options.mk`. An option declared in two places is worse
than one declared in the less elegant place: the debounce decision was once
stated in an ERA `.mk` *and* in five `keyboard.json` files, one name with two
owners, and that is what turned `ERA_DEBOUNCE_ENABLE=no` into a link error.
`era_common_qmk_rules.mk` is the sole owner of `DEBOUNCE_TYPE` now, and no ERA
`keyboard.json` mentions debounce at all.

Three arrangements were live candidates for the rule above, and each costs a
session real time to re-derive because two of them look more attractive than
they are:

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

## Why `<board>.h` is not a declaration site

Every `config.h` and `post_config.h` in the chain is force-included into every
translation unit (`builddefs/common_rules.mk:275`), so a `#define` in one is
exactly as visible as a `-D`. A board header is reached only through
`QMK_KEYBOARD_H`, so whether a unit sees its `#define` depends on that unit's
include list — a property no reader of the option can check and no author of a
new unit is warned about.

The neighbouring shape to the debounce defect above is a **derived** macro
placed there: `ERA_TAP_DANCE_ENABLE` in board headers beside `TAP_DANCE_ENABLE`
in a `.mk`. Nothing disagrees about a value; what is wrong is the home. The two
common units that guard on it without including a board header saw it undefined
and compiled the whole route away, and adding the include to a unit repairs
that unit without generalizing to the next one — which is why the selector is
emitted from `era_tapdance_rules.mk` instead. A macro whose visibility is a
fact about the *reader* rather than about the build has no correct home in a
board header.

## The three shapes the `_ENABLE` suffix still names

Reading them as one thing is what made the dependencies invisible, and the
distinction survives the rule above rather than being replaced by it:

- a **selector** — rule 1: a make variable with a declared default that a build
  may turn on or off, emitted as a `-D`. QMK's own feature switches are this
  shape and so are these;
- **not an option** — rule 5: an unconditional `-D` that only looks like a
  selector, because it is a fact about what an ERA split build is;
- a **board fact** — rule 3: a macro whose value is per board.

## The selectors

**Every firmware selector in the table below is declared in
`keyboards/era/era_build_options.mk`** — one file, and the file a person edits.
The three firmware-inert automated-build controls, `ERA_BUILD_VARIANT`,
`ERA_SHOW_OPTIONS`, and the launcher's internal
`ERA_BUILD_IDENTITY_REPORT`, are declared in
`keyboards/era/era_build_identity_options.mk`
so Brick65 can adopt the same artifact identity without importing an RP2040
firmware default. Two derived values are declared elsewhere for ordering rather
than by drift; they are named after the table. Each ERA firmware `.mk` fragment
includes its declaration file as its first line,
so a fragment cannot run without its declarations; a board assigns above the
`include` that reads it and wins for ordinary firmware options. A selected
build variant is different: every one of its five diagnostic axes is an
`override :=` assignment, so command-line values, `MAKEFLAGS`, exported
environment, and `make -e` cannot mutate the tuple beneath its name.

| Selector | Default | Notes |
| --- | --- | --- |
| `ERA_BUILD_VARIANT` | `standard` | board-independent automated-build identity. `standard`, `wire`, `qwin`, `cause`, `stale` and `qwin_phase` are defined once in `common/build_variants/`; `era_build_variant_rules.mk` applies the selected immutable five-axis tuple to every target and rejects split-only variants on a non-split target, while each diagnostic selector's existing dependency refusal decides whether the selected split board/keymap can build the variant. `sirind/brick65/post_rules.mk` includes that make-time validator and the option printer but no ERA firmware rule, so its only valid identity is firmware-inert `standard`. The launcher requires make's printed tuple to match link-visible ELF witnesses before the resolved name can reach an artifact or manifest; configuration identity is not release approval |
| `ERA_BOARD_COMMON_ENABLE` | `yes` | the class skeleton. Which class a board gets is not this line's decision: `era_common_qmk_rules.mk` adds the non-split unit when `SPLIT_KEYBOARD` is not `yes`, `era_split_qmk_rules.mk` adds the split one, and both add `era_board_hooks.c`. `no` returns a board to writing those QMK hooks itself, and to the double-tap hazard the skeleton retires |
| `ERA_RP2040_MATRIX_ENABLE` | `yes` | Every RP2040 board under `keyboards/era` runs the engine and states `yes` in its own `post_rules.mk`, so what the default decides is what a board saying nothing gets. There is no stock-matrix exception. The engine's raw backend is not a second selector: it is the PIO+DMA sampler alone — `system/era_rp2040_matrix_pio.c`, row drive, settle and column read on a PIO1 state machine, samples DMA'd into a ring, core0 fetching one frame per pass — and `RP_DMA_REQUIRED=TRUE` is emitted beside its `SRC` line as a rule-5 fact, because the sampler takes its two DMA channels from the ChibiOS DMA allocator the ws2812 vendor driver uses and that LLD compiles only under the marker (the platform emits it for `WS2812_DRIVER=vendor` alone; the four ERA boards with no addressable LED need it from here). **There is no second raw backend and no off state.** The alternative was a CPU bit-bang scan, and tuning inside one is duplicated investment by owner decision, so a build cannot select away from the sampler and no selector offers it |
| `ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY` | `128` | the engine's one sub-knob, and its only one: the sampler's settle, in PIO cycles at the CPU clock, read only when the engine is on. The value is a fixed baseline (`era_performance_gates.md`), measured on the scan implementation that preceded the sampler and carried over to it unchanged, so it is rejected downward on device evidence rather than on caution |
| `ERA_SOCD_ENABLE`, `ERA_KKUK_ENABLE`, `ERA_DEBOUNCE_ENABLE`, `ERA_TAPPING_CONFIG_ENABLE` | `yes` | `ERA_TAPPING_CONFIG_ENABLE=no` is refused by name on a split board |
| `ERA_MOUSEKEY_CONFIG_ENABLE` | `yes` | Mouse key tuning and its VIA page on channel 13. `MOUSEKEY_ENABLE=no` beside it is refused by name in make, because the unit assigns to runtime variables `quantum/mousekey.c` defines and that build never compiles. A non-default mousekey mode is refused in `quantum/mousekey.h` instead, because the refusal is about which mode reads the values rather than about which units link. **It carries a rule-5 fact beside its `SRC` line and not a second selector**: `ERA_MOUSEKEY_RUNTIME_DELTA` turns the engine's two per-event step sizes from macros into variables, and it is one fact with the unit that writes them — there is nothing for a board to choose, and a build defining it without the unit gets the same behaviour it had (`era_qmk_fork_ledger.md`) |
| `TAP_DANCE_ENABLE` | `yes` | QMK's own switch; the ERA tap-dance surface is derived from it in `era_tapdance_rules.mk` |
| `ERA_BACKLIGHT_EFFECT_ENABLE` | `no` | The PWM backlight effect layer and its four keyboard-channel value ids. **Defaults off because of the ids rather than the cost**: `0..3` is the band a board may keep for its own handler, and this router runs ahead of the board's, so a board asks for them instead of being given them (`era_identifier_map.md`). `BACKLIGHT_ENABLE=no` beside it is refused by name |
| `ERA_BACKLIGHT_ALWAYS_ON` | `no` | The PWM backlight held on as an indicator supply, for a board whose backlight pin drives nothing but its lock LEDs. It adds no surface — it is what makes *removing* one safe, by repairing a stored `enable = 0` before `backlight_init()` reads it. `BACKLIGHT_ENABLE=no` and `ERA_BACKLIGHT_EFFECT_ENABLE=yes` are each refused beside it by name; the second because the two are contradictory claims about one pin rather than an unsupported combination |
| `ERA_RGB_INDICATOR_ENABLE` | `no` | The RGB Matrix lock-indicator slots and their keyboard-channel value ids `6..12`. **Defaults off for a different reason from the row above, and the two are worth keeping apart**: its ids collide with nothing, and what a board gives up by turning it on is the right to define `rgb_matrix_indicators_kb`, `rgb_matrix_indicators_advanced_kb`, `led_update_kb` and `rgb_matrix_render_policy_kb` itself, because the feature takes all four strongly. `RGB_MATRIX_ENABLE=no` beside it is refused by name, and a board that sets it states `ERA_RGB_INDICATOR_1_LED` — and optionally `_2_LED`, whose presence is what makes the board two-slot — in its own `config.h` under rule 3 |
| `ERA_VIA_BOOTLOADER_ENABLE`, `ERA_EEPROM_CLEAN_ENABLE` | `yes` | all four combinations of the pair are valid; both off drops the ERA VIA command router |
| `ERA_STORAGE_QUIET_DEFER_MS` | `500` | Rule 2's stated exception. Not because no ERA file reads it — `system/era_board_hooks.c` uses its value, and it is the only one (`git grep -l ERA_STORAGE_QUIET_DEFER_MS -- 'keyboards/era/**/*.c' 'keyboards/era/**/*.h'`) — but because the readers that need the **default** are QMK core files preprocessed before any ERA header, so no ERA `#ifndef` can reach them: `quantum/eeconfig.h`, `quantum/rgb_matrix/rgb_matrix.c`, `quantum/rgblight/rgblight.c` and `quantum/via.c`. **Re-derive the ERA reader with the command above rather than trusting this cell** — it has now been wrong twice, once when it said *three* board files after the tomak family's three units became one, and once when the one it named stopped being the reader at all: the keyboard channel's gate replaced that family's local timer on 2026-08-24 |
| `RGB_MATRIX_RENDER_POLICY_ENABLE`, `RGB_MATRIX_RENDER_DOMAIN_ENABLE`, `RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE`, `RGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE` | `yes` | declared only on an `RGB_MATRIX_ENABLE` board; the three sub-options are refused with the policy off |
| `RGB_MATRIX_IDLE_GATE_ENABLE` | `yes` | `rgb_matrix_task()`'s `SYNCING` arm is evaluated once a millisecond on the RP2040 raw microsecond counter instead of once a scan pass. **Rule 1's tie-break case** — a behaviour switch inside a unit compiled either way, emitted as a `-D` and tested with `#if defined`; it inherits that seat from the retired `ERA_SPLIT_INITIATOR_WFE_ENABLE`. Not a policy sub-option and refused by neither of the policy blocks — it gates when the task's own state machine is asked, not what a render does. `no` is the per-pass evaluation the task ran until then. The C side asks for `MCU_RP` as well, so a non-RP2040 board defining it compiles what it did. Its period, `RGB_MATRIX_IDLE_GATE_US` (1000), is rule 2 in `quantum/rgb_matrix/rgb_matrix.c`, its only reader — and one of the two rule-2 knobs the `grep -rn "#ifndef ERA_"` below cannot find, because a core file's macro carries QMK's naming rather than the `ERA_` prefix |
| `ERA_SPLIT_SERIAL_USART_SPEED` | `460800` | **the High link level, not the only one.** The wire runs at one of three levels chosen by the owner and agreed over the wire, and Medium and Low are this value halved and quartered rather than stated separately, so the set stays coherent under a build that moves it. What does not move with it is the SYSTEM page's labels, which name the three rates in text — so `era_split_via_link.c` carries an `#error` refusing a speed those labels were not written for. Both halves still run the identical image, which is the rule the old "flashed with the same value" note was an instance of. **A build that moves it also moves the wire scale**, which the backend derives as `ceil(ERA_SPLIT_SERIAL_USART_SPEED / baud)` — a ceiling, so a value the three levels do not divide rounds toward more window margin rather than less |
| `ERA_SPLIT_EEPROM_SYNC_ENABLE` | `no` | TOMAK_TKL, TOMAK79H and TOMAK79S set `yes` for any keymap with VIA. Requires `VIA_ENABLE` and `RGB_MATRIX_ENABLE`, refused by name in make. `ERA_HOST_PEER_STORAGE_V1_ENABLE` is derived from it — the condition for the storage engine is this feature, never a board identity |
| `ERA_SPLIT_RP_USB_SLEEP_SYNC` | `1` | also accepts the `RP_USB_SYNC_SUSPEND_AFTER_REMOTE_WAKEUP_SET` spelling, the generic name the ChibiOS delta reads |
| `ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE`, `ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE` | `no` | console images; the `wire`/`cause`/`stale` and `qwin` variants set them. `era_split_qmk_rules.mk` derives `ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE` from either — core1's park instrument (parks and microseconds asleep, two timer reads a park) is wanted on every diagnostic image and must not reach the standard image — so that macro is not a selector and is declared nowhere |
| `ERA_PASS_PHASE_DIAGNOSTICS_ENABLE` | `no` | the pass itemised into twelve contiguous segments, printed on the qwin line as `ph=`/`us=`. `era_split_qmk_rules.mk` refuses it without `ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE` by name, because that is the line it prints on. **Its own axis rather than a rider on the two diagnostics selectors**, which is the difference between it and `ERA_SPLIT_CORE1_PARK_DIAGNOSTICS_ENABLE` below: twelve raw counter reads per pass cost scan rate — **measured at 4.13 µs a pass, −14.3 %** — so putting it inside `qwin` would move the comparison point instead of measuring against it. The `qwin_phase` variant is the only thing that sets it, and it also carries the per-segment and whole-pass maxima, which is the half of the instrument a decision about where rendering runs actually uses |
| `ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE`, `ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE` | `no` | each has its preconditions refused by name. The first is the `cause` variant's complete storage-cause instrument: storage phase and edge timelines, indicator edges, and dynamic-macro RAW-HID request/response timing; it is one diagnostic selector rather than separate choices for observations that must share a snapshot boundary |
| the seven split timing knobs | empty | see **The values whose default lives in C** below |

The five axes are ordered `wire,qwin,phase,cause,stale`; the mapping is fixed:

| Variant | Immutable tuple |
| --- | --- |
| `standard` | `no,no,no,no,no` |
| `wire` | `yes,no,no,no,no` |
| `qwin` | `no,yes,no,no,no` |
| `cause` | `yes,no,no,yes,no` |
| `stale` | `yes,no,no,no,yes` |
| `qwin_phase` | `no,yes,yes,no,no` |

The make layer prints the resolved name and labelled tuple only when the
firmware-inert `ERA_BUILD_IDENTITY_REPORT=yes` handshake is requested. The
launcher derives the compiled tuple independently from one link-visible
production witness per axis and refuses any mismatch. The stale axis uses a
zero-storage absolute symbol emitted inside its guarded arm in
`split/era_split_transaction_engine.c`; a callable constant-return witness was
LTO-folded and then removed by `gc-sections`, which is exactly the divergence
this check must expose. Direct option overrides, `MAKEFLAGS`, exported variables
and `make -e` are test inputs, not supported ways to create a seventh
combination.

The performance-batch instruments carry four rule-2 knobs, each an `#ifndef`
in the file that reads it and none in the manifest:
`ERA_RP2040_MATRIX_PIO_ROW_RELEASE_CYCLES` (64) and
`ERA_RP2040_MATRIX_PIO_SAMPLE_RING_FRAMES` (4) in `era_rp2040_matrix_pio.c`,
`ERA_SPLIT_QWIN_SEGMENT_MS` (10000) and `ERA_SPLIT_QWIN_SETTLE_MS` (0) in
`diagnostics/era_split_qwin_diagnostics.c`.

**Two derived values live in `era_split_qmk_rules.mk`, both for ordering rather
than filing.** `ERA_HOST_PEER_STORAGE_V1_ENABLE` is *derived* from
`ERA_SPLIT_EEPROM_SYNC_ENABLE` (`era_split_qmk_rules.mk:16-28`, which also
carries the `$(error)` refusing the storage engine without the sync feature),
and its two-step derivation has to run after a board has set the selector it
derives from, which is after the manifest.
`ERA_SPLIT_COMMUNICATION_CORE_STAGE` accepts exactly `CORE1_FULL` and sits
beside the `$(error)` that says so — it exists to reject a stale board or
variant, not to be chosen.

**The names in the RGB rows have no `ERA_` prefix on purpose.** The macro and
the make variable are one name so a single grep finds the switch and every
reader of it, and the macros are read in `quantum/rgb_matrix/rgb_matrix.c`,
where they are ERA fork edits under QMK's own naming.

**A selector is in this table or it does not exist**, and both ways of leaving
one out are silent. A selector read but never declared still builds, because
`ifeq` on an unset variable is false, so the axis exists only in whichever
profile or board file sets it; a selector emitted as a hand-copied `OPT_DEFS`
line in board files has no default, no declaration and no off state at all
while every reader of it is guarded and looks correct.

## The values whose default lives in C

Seven split timing knobs are rule 2 done correctly — each default is a
`#ifndef` in the file that reads it, and make carries only a guarded
pass-through so a build can override one. They are
`ERA_SPLIT_PEER_RESPONSE_WINDOW_MS` and
`ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS` (defaults in
`era_split_wire_protocol.h`), and `ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER`,
`..._BACKOFF_PERIOD_MS`, `ERA_SPLIT_SESSION_REFRESH_PERIOD_MS`,
`ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS` and
`ERA_SPLIT_SESSION_STALE_MS` (defaults in
`scheduler/era_split_transport_scheduler_internal.h`).

Each carries an **empty `?=`** in the manifest, which leaves every pass-through
false and every default in C and buys only that the axis is on the page. That
is the whole point of the declaration: a knob read but never declared is an
axis that exists only in whichever profile or board file sets it.

## The VIA surface and its gate

**A build without VIA links no ERA VIA code** (owner decision). The
gate is `ifeq ($(strip $(VIA_ENABLE)), yes)` in `era_common_qmk_rules.mk`,
`era_tapdance_rules.mk` and `era_split_qmk_rules.mk`, and it is possible only
because the ERA layer runs in the `post_rules.mk` phase; in the `rules.mk` phase
the same test would have been silently false and compiled the surface away from
*every* build.

**The closure is exact, and that is what makes the gate a rule rather than a
list.** Every export of every unit under it — `era_common_via.c`,
`era_via_system.c`, every feature `*_via.c` adapter, `era_split_via_sync.c`
— is called only from `era_common_via.c` or from
`era_split_keyboard_handle_via_command()`, and that function is reached only
from each board's `via_custom_value_command_kb()`, which QMK calls only from
`via.c` and which every board file already guards on `VIA_ENABLE`.
`ERA_VIA_BOOTLOADER_ENABLE` and `ERA_EEPROM_CLEAN_ENABLE` are read in
`era_via_system.c` and nowhere else, so their `-D` sits under the same gate:
emitting a selector whose only reader is not compiled is how a switch comes to
look live when it is not.

**The header declarations are deliberately left ungated**, which is the
treatment `era_split_transport_scheduler.h`'s diagnostics exports already take:
a future non-VIA caller then fails at link instead of silently pulling the
surface back in.

## Where the options live, and how to see what a build used

| File | Its job |
| --- | --- |
| `keyboards/era/era_build_identity_options.mk` | the three firmware-inert controls every ERA target shares: `ERA_BUILD_VARIANT`, `ERA_SHOW_OPTIONS`, and the internal `ERA_BUILD_IDENTITY_REPORT` handshake. It must contain no QMK feature switch or ERA firmware selector, because Brick65 includes it without adopting the firmware layer |
| `keyboards/era/era_build_options.mk` | **every firmware selector declaration.** The one file to edit for firmware configuration |
| `system/era_common_qmk_rules.mk` | the phase guard, and the `ifeq`/`SRC`/`-D` for the matrix engine, the feature selectors, the VIA surface, and the non-split class skeleton with the `SPLIT_KEYBOARD` test that keeps a split board off it. The count is deliberately not written: it read *four* from before the backlight feature landed until 2026-08-18, when a second lighting feature would have made it six |
| `system/era_build_variant_rules.mk` | the common variant name validation, the refusal of split-only variants on non-split targets, the single include that applies a selected complete combination before any diagnostic selector is consumed, and the machine-readable resolved identity line |
| `common/build_variants/*.mk` | the complete diagnostic-selector combination for `standard`, `wire`, `qwin`, `cause`, `stale` and `qwin_phase`. Each file states all five axes with `override :=`, so direct assignments, `MAKEFLAGS`, environment and `make -e` cannot produce a differently instrumented artifact under the same name |
| `tools/era_qmk_build.sh` | the WSL-local launcher: requests the make identity line, rejects a requested/resolved mismatch, derives the five compiled axes from the ELF, rejects a resolved/compiled mismatch, and only then names and records the artifact from the resolved variant |
| `split/era_split_qmk_rules.mk` | the split relation's `SRC` block, its `$(error)` refusals, the storage derivation and the stage |
| `features/era_tapdance_rules.mk` | the tap-dance units and the ERA surface derived from QMK's switch |
| `system/era_rgb_matrix_rules.mk` | the RGB render policy emissions and their refusal |
| `split/era_split_usb_sleep_rules.mk` | the RP USB sleep sync emission |
| `system/era_sram_resident_rules.mk` | nothing declarable — the copy-to-RAM bundle is an include and deliberately not a variable, because a partial adoption has to fail the link |
| `storage/era_storage_adoption_rules.mk` | nothing declarable either, and for the sibling reason — the EEPROM adoption bundle is an include because its parts fail as a set and one of its values is a macro rather than a number. Its only make-visible product is the marker `era_split_qmk_rules.mk` refuses EEPROM sync without (**Storage Adoption**) |
| each board's `post_rules.mk` | **the feature set that board ships, written out explicitly** — every feature option at its value, above the includes that read them (owner decision): a maintainer opening a board file should see which features exist and which can be turned off, without first knowing that an unstated option has a default somewhere else. Diagnostics and timing axes are deliberately absent: they are per-build instruments, not board features |
| `system/era_show_options.mk` | the printer below |

**Two places hold a value, and they are not two authorities.** The declaration
declares with `?=` — the name, the documentation, and the value a board gets if
it says nothing. A board `post_rules.mk` states with `=` what that board
actually ships, and the board wins: the file a person opens to change one board
is the file that decides it. The cost is that a new option is added in two
places, and the check is the printer below rather than a rule anyone has to
remember — a board whose list is short next to what the printer reports has an
option it never stated.

**`era-build --show-options <board>:<keymap>`** prints every
ERA option with its value and its origin — `file` for a default or a board
line, `command line` for a `-e`, `environment` for an exported variable — and
then builds normally. It answers *what did this build actually use*, which the
artifact manifest answers only at the canonical variant boundary: the manifest
records `requested_variant=`, resolved `variant=`, `resolved_tuple=`,
`compiled_tuple=`, and the exact `-e ERA_BUILD_VARIANT=...` command, while this
printer expands that variant into its resolved selector values. Artifact naming
also uses the resolved variant. Its set is derived
from make's own variable list rather than enumerated, so a new option appears
automatically and a new non-option matching the pattern appears until it is
excluded — it can show something that is not an option, and **it cannot
silently omit a make-declared one**, which is the failure direction that
matters.

**That last claim was once false for the RGB half of the pattern, in exactly
the way it forbids**, and it is recorded because the
shape recurs: the filter enumerated `RGB_MATRIX_RENDER_%` and
`RGB_MATRIX_INDICATORS_%`, the two prefixes that existed when it was written, so
it was a list wearing a pattern's clothes and `RGB_MATRIX_IDLE_GATE_ENABLE` was
declared, emitted, read and built without ever appearing. The filter is
`RGB_MATRIX_%` now and the two QMK-owned names it catches are excluded by name.
**A derived set is only derived if its predicate is one a new member satisfies
by existing**; a prefix list is not that, and neither is anything else that has
to be extended when the thing it describes grows.

**That guarantee is scoped, and the scope is the other half of the surface.**
It cannot see a value whose only declaration is a `#ifndef` in the C file that
reads it, and there are about forty of those — from the DUAL-HOST poll period
to the core1 stack size and the responder ring capacities. They are rule 2
working as designed rather than a gap: the file that reads the value is where a
person edits it. The seven split timing knobs are in the manifest only because
make already carried a pass-through for them, so they are seven of about
forty-seven rather than a complete class, and
`grep -rn "#ifndef ERA_" keyboards/era` enumerates the rest.

## The dependencies, and which layer each can be declared in

**The ERA option layer runs from each board's `post_rules.mk`, which QMK
includes after the keymap's `rules.mk`**, so every QMK feature switch is
visible to it. `builddefs/build_keyboard.mk` takes the keyboard `rules.mk`
chain at `:101-105`, the `keyboard.json`-generated `info_rules.mk` at `:143`,
the keymap's `rules.mk` at `:146`, and `post_rules.mk` at `:445-449` — before
`INTROSPECTION_KEYMAP_C` is consumed at `:473` and before
`common_features.mk`/`generic_features.mk` at `:494-495` read `CUSTOM_MATRIX`,
`DEBOUNCE_TYPE`, `SPLIT_TRANSPORT` and every `X_ENABLE` → `-DX_ENABLE`. Nothing
ERA sets is consumed before `:449`. So both kinds of dependency can be stated
in make: an ERA-selector-to-ERA-selector edge, and one on a QMK feature.

**The `rules.mk` phase is not the phase ERA uses, and confusing the two is the
one wrong turn here.** It is true that QMK includes the keymap's `rules.mk`
after the *keyboard's*, so `VIA_ENABLE` is unset in that phase; the conclusion
that a QMK-feature dependency therefore has nowhere to go but a C `#error` does
not follow, because QMK gives a keyboard a **second** make phase for exactly
this purpose (`docs/hardware_keyboard_guidelines.md`), which upstream
`cipulot/common`, `ploopyco`, `rgbkb/sol/rev2` and `system76/launch_1` use. A
dependency stated in neither layer is still the defect.

A fragment included from the wrong phase still builds and silently loses the
conditional half, so `era_common_qmk_rules.mk` refuses it outright rather than
documenting it — the guard and the ordering it rests on are written there.

| Depends on | Declared where |
| --- | --- |
| split transport → `ERA_RP2040_MATRIX_ENABLE`, `NO_USB_STARTUP_CHECK=yes` | `era_split_qmk_rules.mk`, `$(error)` |
| `..._CAUSE_TIMELINE_ENABLE` → wire diagnostics + EEPROM sync + storage V1 | `era_split_qmk_rules.mk`, `$(error)` |
| `..._MIRROR_FORCE_STALE_ENABLE` → wire diagnostics | `era_split_qmk_rules.mk`, `$(error)` |
| `ERA_HOST_PEER_STORAGE_V1_ENABLE` → `ERA_SPLIT_EEPROM_SYNC_ENABLE` | `era_split_qmk_rules.mk`, derived plus `$(error)` |
| `ERA_DEBOUNCE_ENABLE` shares its runtime unit with `ERA_RP2040_MATRIX_ENABLE` | `era_common_qmk_rules.mk`, one `SRC` line each way |
| `ERA_DEBOUNCE_ENABLE` owns `DEBOUNCE_TYPE` | `era_common_qmk_rules.mk` **alone**. A board `keyboard.json` also stating `build.debounce_type: custom` is rule 4's failure — two owners for one decision, so turning the ERA selector off leaves `DEBOUNCE_TYPE = custom` standing with nothing supplying `debounce()` |
| ERA split → `ERA_TAPPING_CONFIG_ENABLE=yes` | `era_split_qmk_rules.mk`, `$(error)` beside the tap-activity `SRC` line. The cross-half judgment window is derived from the tap-hold policy that unit persists, and the family is unconditional in a split build, so the two genuinely do not compose |
| KKUK ↔ SOCD | `era_kkuk.c`, `#ifdef` with a `static inline` false. This one is a composition rather than a requirement — with SOCD compiled out no keycode is SOCD-bound, so the interlock has a constant answer rather than a missing one |
| `ERA_VIA_SYSTEM_ENABLE` ← `ERA_VIA_BOOTLOADER_ENABLE` or `ERA_EEPROM_CLEAN_ENABLE` | `era_common_qmk_rules.mk`, derived; the `#ifdef` at the router entry in `era_common_via.c` is what makes all four combinations of the pair valid |
| the three common lighting selectors → the QMK feature each layers over: `ERA_BACKLIGHT_EFFECT_ENABLE` and `ERA_BACKLIGHT_ALWAYS_ON` → `BACKLIGHT_ENABLE`, `ERA_RGB_INDICATOR_ENABLE` → `RGB_MATRIX_ENABLE` | `era_common_qmk_rules.mk`, one `$(error)` each. None is a driver, so without its feature every call in the unit is to a symbol that is not there — which make can refuse in one line before any compile instead of raising a page of undefined references at the link |
| `ERA_BACKLIGHT_ALWAYS_ON` ↮ `ERA_BACKLIGHT_EFFECT_ENABLE` | `era_common_qmk_rules.mk`, `$(error)`. **The one refusal here that is not a missing dependency**: both build, and what they disagree about is the hardware. The effect layer's keypress blinks drive the rail to zero deliberately, which is the state the other one says the rail may never be in — so a board asserting both is describing two different pins |
| storage engine → `VIA_ENABLE` and `RGB_MATRIX_ENABLE` | `era_host_peer_storage.c`, `#error` |
| **the split INPUT layer section → a one-byte `layer_state_t`** | `era_split_qmk_rules.mk` supplies `LAYER_STATE_8BIT`; the bound is in `era_split_peer_layer.c` |
| ERA split → no `BACKLIGHT`/`RGBLIGHT`/`LED_MATRIX`/`SLEEP_LED`/`OLED`/`ST7565` | `era_split_qmk_rules.mk`, `$(error)` naming whichever is set. `NO_USB_STARTUP_CHECK` deletes QMK's suspend loop, so the split lighting-sleep apply is ERA's own and it drives RGB Matrix only; any other lighting would build, run, and never sleep. The refusal is what turns that from a behaviour discovered on a rig into a decision made on purpose — adding one means extending the split apply and deleting its line |

**The last row is the one to read whole**, because leaving it undeclared is
what stops an ERA split board building a non-VIA keymap at all. The INPUT
section carries the peer's layer in one wire byte, so the relation admits eight
layers. QMK derives `layer_state_t` from `DYNAMIC_KEYMAP_LAYER_COUNT` and falls
back to **sixteen** bits when nothing supplies one — which is every keymap
without a dynamic keymap, which is every keymap without VIA. The build then
failed a `_Static_assert` whose message named a layer count the board had never
chosen. The split rules supply `LAYER_STATE_8BIT` unconditionally now, because
the eight-layer limit is a wire-format fact rather than a preference, and
because make cannot ask whether VIA is on. That define also *outranks* the
sixteen-bit choice `action_layer.h` makes for itself above eight layers, so it
could have satisfied the assert by truncating — the bound against that is a
`#error` on `DYNAMIC_KEYMAP_LAYER_COUNT` beside the assert, and the two are read
together or neither is worth anything.

**One dependency is board content rather than architecture**, recorded so it is
not mistaken for the class above: `tomak79s`'s indicator configuration can only
change through a VIA write or an EEPROM-sync reload, because that board has no
keycode handler for it where `tomak` does. It is no longer a *build* dependency,
and how it stopped being one is worth a line because the same shape recurs: while
each board carried its own copy of the helpers, they were `static`, so a build
with both features off left them uncalled and `-Werror=unused-function` reported
it correctly — which is why that file carried a compound `#define` naming both
conditions at the top. With the helpers in `sirind/common/tomak_common.c` they
are exports of a shared unit, an unreferenced one is removed by
`--gc-sections` instead, and the guard question does not arise. **Moving a
`static` into a shared unit retires its guard**; it does not retire the fact the
guard described.

## What a build combination is expected to do

Two properties are checked rather than assumed, and a change is held to both.

**Every board builds both its `default` and its `via` keymap.** The board ×
keymap set is **forty-four combinations**, twenty-two RP2040 boards × two
keymaps, and every one of them also passes the three copy-to-RAM gates. A `default` build is
the ERA performance work without the VIA surface: the copy-to-RAM image, the
matrix engine, the debounce runtime, the split relation and its wire are all
present; what is absent is the VIA UI and, with it, anything reachable only
from a VIA value write.

**A declared selector's off state builds.** A declared axis that cannot be
exercised is worse than an undeclared one, because the declaration is a
promise. Four different causes have broken this, and keeping them apart is what
makes the property actionable:

- an **unguarded cross-feature call** — `era_kkuk.c` asks SOCD which keycodes
  it owns, and did so unconditionally, so turning SOCD off produced an
  undefined reference at KKUK, a feature the person did not touch;
- a **unit left in `SRC`** under a derived selector while half its own body was
  guarded away, so it referenced its own missing statics
  (`ERA_EEPROM_CLEAN_ENABLE` under `ERA_VIA_SYSTEM_ENABLE`);
- **one decision with two owners** — rule 4's failure: `keyboard.json` kept
  `DEBOUNCE_TYPE = custom` standing after `ERA_DEBOUNCE_ENABLE` stopped
  supplying `debounce()`;
- a **real non-composition**. `ERA_TAPPING_CONFIG_ENABLE=no` is this one and is
  the single off state that still does not build on a split board — but it
  stops before any compile, naming both selectors, instead of raising three
  undefined references at a caller that mentions neither. On a non-split board
  it turns off cleanly.

A feature that genuinely cannot be turned off in some configuration is a
legitimate answer; a link error at an unrelated caller is not, and the
distinction is what these two properties are for.

**Four `sirind/tomak` layout-variant keymaps sit beside that set and build.**
`default_ansi`, `default_ansi_split_bs`, `default_ansi_split_rshift` and
`default_ansi_split_rshift_bs` are the 97-key `default` keymap under a
`LAYOUT_` macro that takes 94, 95, 95 or 96; each layer is the `default` layer
filtered by that layout's own `matrix` list, in that list's order — a
derivation, not a choice, and the one the board's own layer 0 already
followed. Their layer 1 was a copy of the full 97-key layer for a while, which
is what stopped them building; whoever touches one next re-derives it from
`default` the same way rather than editing it by hand.
