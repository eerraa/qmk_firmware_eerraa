# ERA firmware build options -- the declaration site.
#
# Every ERA firmware selector is declared here with the value a board that says
# nothing gets. Automated-build identity is the deliberately firmware-inert
# exception in keyboards/era/era_build_identity_options.mk. **Editing a value
# here does not change what a board builds**, because
# every board assigns above the include that reads this file and a board's
# assignment wins: measured 2026-08-18 over the twenty-two board
# `post_rules.mk` files, 293 assignments restate the default they are given
# here verbatim and 18 differ -- `ERA_SPLIT_EEPROM_SYNC_ENABLE` on the three
# split boards, and the three lighting selectors on the fifteen boards that ask
# for one. Ten declared names are stated by no board and are the only ones a
# `?=` here still decides -- `ERA_BOARD_COMMON_ENABLE`, the five diagnostics
# selectors and four of the seven split timing knobs.
#
# So: to change one board, edit that board's `post_rules.mk`; to change one
# build, pass `-e`. To change what a board with no line of its own gets, edit
# here.
#
#   qmk compile -kb era/sirind/tomak -km via -e ERA_SHOW_OPTIONS=yes
#
# prints what a build actually used and where each value came from, which is
# the one question this page cannot answer for you.
#
# Included by each ERA firmware `.mk` fragment as its first line rather than by
# the board, so a fragment cannot run without its declarations. The identity
# validator and printer instead include the firmware-inert identity declaration
# file. A board assigns above the `include` that reads it and wins; the selected
# common build variant assigns its diagnostic combination after this declaration
# layer. That is why every line here is `?=`.
#
# Not here, deliberately:
#   - the SRC lines, the -D emissions and the $(error) refusals stay in the
#     fragment that owns each feature, so a combination this page cannot
#     express is still refused by name before any compile;
#   - ERA_HOST_PEER_STORAGE_V1_ENABLE is DERIVED from the sync selector in
#     split/era_split_qmk_rules.mk and must stay there: the derivation has to
#     run after a board has set the selector it derives from;
#   - ERA_SPLIT_COMMUNICATION_CORE_STAGE accepts exactly CORE1_FULL and is
#     declared beside the $(error) that says so. It exists to reject a stale
#     board or variant, not to be chosen;
#   - the copy-to-RAM image is an include, not a variable
#     (system/era_sram_resident_rules.mk), because a partial adoption has to
#     fail the link rather than build;
#   - anything with no off state is in era_source_map.md's Build Selectors
#     section under the class that says why.
#
# Do not rename this file to rules.mk or post_rules.mk. QMK auto-includes both
# names from keyboards/era -- the first in a phase that cannot see the keymap,
# the second after the board file that needs these values.

ifndef ERA_BUILD_OPTIONS_INCLUDED
ERA_BUILD_OPTIONS_INCLUDED := yes

# --- Build identity ---------------------------------------------------------
# This lightweight declaration is shared with Brick65 without importing any of
# the firmware defaults below. `standard` means the board/keymap feature set
# with all diagnostic selectors off.
include keyboards/era/era_build_identity_options.mk

# --- Board layer -----------------------------------------------------------
# The class skeleton: the QMK hooks every ERA board of a class wires
# identically -- housekeeping_task_kb, the init trio, process_record_kb and one
# via_custom_value_command_kb -- owned by the common layer instead of copied
# into each board .c. Which class you get is decided by the board, not by this
# line: system/era_common_qmk_rules.mk adds the non-split unit and
# split/era_split_qmk_rules.mk adds the split one.
#
# What a board adds on top of the skeleton is the weak hook set in
# system/era_board_hooks.h. What it cannot do is *replace* one of those five
# hooks: a second strong definition fails the link. A board that needs a
# different body for one of them sets this to no and writes all five itself,
# which is what every board did before 2026-08-13.
#
# yes is the default because of what the skeleton owning housekeeping_task_kb
# buys: era_common_features_task() is the only closer of the double-tap
# bootloader window, and a board .c that stopped calling it entered BOOTSEL on
# every reset permanently with nothing mechanical to catch it
# (era_source_map.md's Non-Split Board Baseline).
ERA_BOARD_COMMON_ENABLE ?= yes

# --- Matrix engine ---------------------------------------------------------
# The ERA RP2040 scan engine. no leaves QMK's stock matrix in place.
#
# yes is the ERA default policy since 2026-08-11: every board in keyboards/era
# runs the ERA engine, in its default keymap build as much as its via one. The
# default was no while the engine was a per-board adoption, and every board
# states it in its own post_rules.mk anyway, so what this line decides is only
# what a board that says nothing gets -- and what that board should get is an
# ERA board. Eligibility is RP2040, COL2ROW, and no matrix source of its own;
# era_rp2040_matrix_pio.c rejects the first two by name at compile time rather
# than degrading quietly, which is what makes the default safe to state this
# way.
#
# The engine's raw backend is the PIO+DMA sampler (performance batch 1,
# 2026-08-16): row drive, settle and column read on a PIO1 state machine,
# samples DMA'd into a ring, core0 reading one frame per pass. It is not a
# selector -- there is no other backend to select. The CPU engine that was its
# off state during the batch, and the two knobs only that engine read, retired
# when the batch closed: no ERA image linked it, and tuning inside a CPU
# bit-bang scan is duplicated investment by owner decision. The retired unit is
# deliberately not named -- it is in neither this tree nor the history a reader
# of this file holds, so a name here would be a pointer to nothing.
ERA_RP2040_MATRIX_ENABLE ?= yes
# Cycles a driven row is held before the columns are sampled -- the sampler's
# settle, in PIO cycles at the CPU clock, so the fixed baseline measured on the
# CPU engine (era_performance_gates.md: 32 and 64 rejected on device) carries
# over unchanged. Lower it only with a measured scan rate in hand.
ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY ?= 128

# --- Keyboard features -----------------------------------------------------
# SOCD resolution and its VIA page.
ERA_SOCD_ENABLE ?= yes
# KKUK and its VIA page. Independent of SOCD in either direction.
ERA_KKUK_ENABLE ?= yes
# The VIA-configurable debounce layer. This selector alone owns DEBOUNCE_TYPE;
# no returns the build to QMK's stock debounce.
ERA_DEBOUNCE_ENABLE ?= yes
# The tap-hold policy unit and its VIA page. An ERA split build requires yes --
# the cross-half judgment window is derived from what this unit stores, and the
# split fragment refuses no by name.
ERA_TAPPING_CONFIG_ENABLE ?= yes
# Mouse key tuning and its VIA page: six controls over QMK's own default
# accelerated mouse engine, written into the runtime variables that engine
# already keeps. It adds no engine and no scan-path work. MOUSEKEY_ENABLE=no
# beside it is refused by name; a non-default mousekey mode (MK_3_SPEED,
# MK_COMBINED, MK_KINETIC_SPEED, MOUSEKEY_INERTIA) is refused in
# quantum/mousekey.h, because the controls name values only the default mode
# reads.
ERA_MOUSEKEY_CONFIG_ENABLE ?= yes
# QMK tap dance, and with it the ERA tap-dance surface and its VIA page.
TAP_DANCE_ENABLE ?= yes
# The PWM backlight effect layer and its four keyboard-channel value ids:
# brightness, effect, breathing period, blink speed. Default no because the
# ids sit in the band a board may keep for its own handler, so a board asks for
# them rather than being given them -- see era_backlight_via.h. Requires QMK's
# BACKLIGHT_ENABLE, which the fragment refuses by name; the two blink effects
# need no extra QMK switch and breathing degrades to steady without
# BACKLIGHT_BREATHING.
ERA_BACKLIGHT_EFFECT_ENABLE ?= no
# The PWM backlight as an indicator supply rather than as lighting: on a board
# whose backlight pin drives nothing but its lock LEDs, a stored `enable = 0`
# is a dark keyboard rather than a preference, so this repairs the stored block
# at init and the board ships no lighting surface at all (owner decision
# 2026-08-18). Requires BACKLIGHT_ENABLE and is refused together with
# ERA_BACKLIGHT_EFFECT_ENABLE, which is the same pin claimed as a backlight.
ERA_BACKLIGHT_ALWAYS_ON ?= no
# The RGB Matrix lock-indicator slots and their seven keyboard-channel value
# ids (6..12): a master role switch, then a lock source, a brightness and a
# colour per slot. Default no because the feature takes QMK's own weak
# `rgb_matrix_indicators_kb`, `rgb_matrix_indicators_advanced_kb`,
# `led_update_kb` and `rgb_matrix_render_policy_kb` as strong definitions, so a
# board that turns it on gives up defining any of them itself -- which is why a
# board asks for it rather than being given it. Requires RGB_MATRIX_ENABLE,
# refused by name, and each board states which LED each slot paints in its own
# config.h (ERA_RGB_INDICATOR_1_LED / _2_LED).
ERA_RGB_INDICATOR_ENABLE ?= no

# --- VIA system commands ---------------------------------------------------
# The VIA "jump to bootloader" command.
ERA_VIA_BOOTLOADER_ENABLE ?= yes
# The VIA EEPROM CLEAN command, which restarts the board afterwards.
# Both off drops the ERA VIA command router; all four combinations are valid.
ERA_EEPROM_CLEAN_ENABLE ?= yes

# --- Storage ---------------------------------------------------------------
# Milliseconds of quiet before a deferred EEPROM write is committed.
ERA_STORAGE_QUIET_DEFER_MS ?= 500

ifeq ($(strip $(RGB_MATRIX_ENABLE)), yes)
# --- RGB Matrix render work ------------------------------------------------
# The ERA render policy. Turning it off requires the three below to be off too;
# the fragment refuses any other combination rather than sanitizing it away.
RGB_MATRIX_RENDER_POLICY_ENABLE ?= yes
# Per-domain render scheduling.
RGB_MATRIX_RENDER_DOMAIN_ENABLE ?= yes
# Indicators render independently of the effect frame.
RGB_MATRIX_INDICATORS_INDEPENDENT_ENABLE ?= yes
# Indicators still render with the RGB effect switched off.
RGB_MATRIX_INDICATORS_WHEN_DISABLED_ENABLE ?= yes
# The RGB task's idle arm is evaluated once a millisecond instead of once a
# scan pass. RP2040 only, on the raw microsecond counter. The arm decides two
# millisecond facts -- the frame limit and the deferred eeconfig flush -- so a
# millisecond sample reads what a per-pass one reads; the rendering, flushing
# and starting arms are untouched and still run on every pass. no is the
# per-pass evaluation this ran with until 2026-08-16. The period itself is a
# #ifndef in quantum/rgb_matrix/rgb_matrix.c, which is its only reader.
RGB_MATRIX_IDLE_GATE_ENABLE ?= yes
endif

ifeq ($(strip $(SPLIT_KEYBOARD)), yes)
# --- Split relation --------------------------------------------------------
# Half-to-half USART bit rate. Both halves must be flashed with the same value.
ERA_SPLIT_SERIAL_USART_SPEED ?= 460800
# Cross-half EEPROM sync, and with it the whole replacement-storage engine.
# It requires VIA and RGB Matrix; the split fragment says so by name.
ERA_SPLIT_EEPROM_SYNC_ENABLE ?= no
# RP2040 USB LLD: 1 re-syncs controller suspend state after a remote wakeup,
# 0 is stock ChibiOS.
ERA_SPLIT_RP_USB_SLEEP_SYNC ?= 1
# --- Split diagnostics: console images, never flashed to an owner ----------
# Full wire and raw-scan diagnostics. Forces CONSOLE_ENABLE.
ERA_SPLIT_WIRE_DIAGNOSTICS_ENABLE ?= no
# Count-only scan-rate diagnostics. Not to be combined with the line above.
ERA_SPLIT_QWIN_COUNT_ONLY_ENABLE ?= no
# The pass, itemised: every microsecond of a keyboard pass charged to one of
# twelve contiguous segments (scan, debounce, transport step, difference loop,
# quantum task, RGB task, main-loop tail, housekeeping ...), printed on the qwin
# line as `ph=`/`us=`. Requires the count-only instrument above, which owns that
# line. Its own axis rather than a rider on either diagnostics selector,
# because twelve counter reads per pass cost scan rate and folding them into
# `qwin` would move the comparison point: the qwin_phase profile carries it, and
# the cost is read as scan_hz(qwin) - scan_hz(qwin_phase) in one sitting.
ERA_PASS_PHASE_DIAGNOSTICS_ENABLE ?= no
# Storage cause timeline. Requires wire diagnostics and EEPROM sync.
ERA_HOST_PEER_STORAGE_CAUSE_TIMELINE_ENABLE ?= no
# Injection, never a production image: every era-boundary mirror read takes its
# fallback so a block must report meas=0. Requires wire diagnostics.
ERA_SPLIT_ERA_MIRROR_FORCE_STALE_ENABLE ?= no

# --- Split timing: empty means the C file's default applies ----------------
# Put a number here only to override the #ifndef that holds the default. The
# default lives in C because that is where the reader is; make carrying it
# would make the C file unreadable as documentation. The first two are in
# split/era_split_wire_protocol.h, the other five in
# split/scheduler/era_split_transport_scheduler_internal.h.
#
# Every other ERA value with a #ifndef default -- and there are about forty,
# from the DUAL-HOST poll period to the core1 stack size -- is set the same
# way, by editing that #ifndef. They are not listed here: this page is the
# make surface, and `grep -rn "#ifndef ERA_" keyboards/era` is the other one.
ERA_SPLIT_PEER_RESPONSE_WINDOW_MS ?=
ERA_SPLIT_SESSION_BOOTSTRAP_RESPONSE_WINDOW_MS ?=
ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_AFTER ?=
ERA_SPLIT_SESSION_BOOTSTRAP_BACKOFF_PERIOD_MS ?=
ERA_SPLIT_SESSION_REFRESH_PERIOD_MS ?=
ERA_SPLIT_HOST_SOURCE_RESPONSE_POLL_PERIOD_MS ?=
ERA_SPLIT_SESSION_STALE_MS ?=
endif

endif
