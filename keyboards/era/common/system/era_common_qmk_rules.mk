# Every ERA option this file reads is declared in keyboards/era/era_build_options.mk,
# which is the one file to edit. Included here rather than by the board so this
# fragment cannot run without its declarations; a board or a variant assigning
# above the include that reads it still wins, because every line there is `?=`.
include keyboards/era/era_build_options.mk

# ERA's option layer runs in QMK's post_rules.mk phase, and this is the check
# that it did. builddefs/build_keyboard.mk includes the keyboard rules.mk chain
# at :101-105 and the keymap's rules.mk at :146, so a fragment included from a
# board rules.mk cannot see VIA_ENABLE or any other keymap-set switch. It still
# builds -- it silently loses the conditional half -- which is why the wrong
# phase has to be refused rather than documented. post_rules.mk is included at
# :445-449, after the keymap's rules.mk and before every consumer ERA has:
# INTROSPECTION_KEYMAP_C at :473, and common_features.mk/generic_features.mk at
# :494-495 for CUSTOM_MATRIX, DEBOUNCE_TYPE, SPLIT_TRANSPORT and every
# X_ENABLE -> -DX_ENABLE.
#
# KEYMAP_PATH is assigned in builddefs/locate_keymap.mk, included at :145, so it
# is empty during the rules.mk pass and set during the post_rules.mk one. Its
# emptiness is exactly the phase test and nothing else has to be maintained.
ifeq ($(strip $(KEYMAP_PATH)),)
    $(error ERA build rules must be included from a board post_rules.mk, not rules.mk -- see the phase note in era_common_qmk_rules.mk)
endif

# Apply the repository-wide build identity after the keymap and board feature
# lines are visible, but before any common fragment consumes a diagnostic
# selector. This include is reached directly by non-split boards and through
# era_split_qmk_rules.mk by split boards, so no board owns a second variant
# axis.
include keyboards/era/common/system/era_build_variant_rules.mk

# Read by era_sram_resident_rules.mk, which cannot be taken without this file.
# The reason is at the refusal there, not here: this file supplies both the
# other half of the double-tap window and its only closer.
ERA_COMMON_QMK_RULES_INCLUDED := yes

# No ERA image links an allocator (era_invariants.md, the Source Gate), and the
# cheapest way that stops being true is a QMK path reaching for newlib rand():
# its reent form drags in malloc. lib8tion carries a generator that does not, so
# ERA takes it on every board -- an RGB Matrix board already had it, and an
# RGBLIGHT board did not, which is how newone/odessey60s came to link malloc
# from the twinkle effect and ship that way from the day it went copy-to-RAM.
LIB8TION_ENABLE = yes

SRC += keyboards/era/common/storage/era_eeprom_config_io.c
SRC += keyboards/era/common/system/era_common_features.c

# The class skeleton and the weak hook set behind it. The selector is declared
# in the manifest with what it buys; what belongs here is which unit each class
# gets, and why this file can decide that at all.
#
# SPLIT_KEYBOARD arrives from keyboard.json's split.enabled through
# info_rules.mk at build_keyboard.mk:143, which is before post_rules.mk at
# :445, so it is already set when this fragment runs -- the same fact
# era_split_qmk_rules.mk's own refusal rests on. This file is included by
# split/era_split_qmk_rules.mk as well as directly by a non-split board, so the
# test is what keeps a split board from linking the non-split skeleton on its
# way to the split one.
#
# era_board_hooks.c is added for both classes: it is the weak defaults the
# skeleton falls through to, in one unit rather than one copy per class.
ifeq ($(strip $(ERA_BOARD_COMMON_ENABLE)), yes)
    SRC += keyboards/era/common/system/era_board_hooks.c
    ifneq ($(strip $(SPLIT_KEYBOARD)), yes)
        SRC += keyboards/era/common/system/era_nonsplit_board.c
    endif
endif

# The frame-loss half of the ERA sleep decision. Unconditional: it is a fact
# about USB rather than a feature, and the arm self-limits to a board whose
# lighting sleep is enabled -- the quantum entry points it drives compile to
# nothing where RGB_MATRIX_SLEEP / RGBLIGHT_SLEEP are absent.
SRC += keyboards/era/common/system/era_usb_session.c

# Guarded like every other value pass-through in this layer. Unguarded, an
# empty value emitted `-DERA_STORAGE_QUIET_DEFER_MS` with no body and failed
# inside quantum/eeconfig.h rather than here.
ifneq ($(strip $(ERA_STORAGE_QUIET_DEFER_MS)),)
    OPT_DEFS += -DERA_STORAGE_QUIET_DEFER_MS=$(strip $(ERA_STORAGE_QUIET_DEFER_MS))
endif

# Arm the RP2040 double-tap bootloader window without the busy wait in
# __late_init; era_common_features_task() closes it on the same timeout. Boards
# outside this common layer keep the upstream blocking wait, which is what makes
# the selector necessary: without a caller for the task the window never closes.
OPT_DEFS += -DRP2040_BOOTLOADER_DOUBLE_TAP_RESET_NONBLOCKING

# The platform millisecond clock answers from a once-per-millisecond cache
# (platforms/chibios/timer.c; the row is in era_qmk_fork_ledger.md). A fact
# about every ERA image rather than a feature, which is why it is emitted
# unconditionally and declared nowhere: timer_read32() is asked at the matrix
# scan rate, each unconverted call is about 2 us of system lock and 64-bit
# arithmetic on this platform -- 16 % of core0 at 22 kHz, and the whole of the
# 2026-08-15 scan-rate regression -- and the value it returns moves 1000 times
# a second. The cached value is bit-identical to the converted one, so no
# consumer sees a different clock. Boards outside this layer keep upstream's
# per-call conversion.
OPT_DEFS += -DERA_TIMER_MS_CACHE

ifeq ($(strip $(ERA_RP2040_MATRIX_ENABLE)), yes)
    OPT_DEFS += -DERA_RP2040_MATRIX_ENABLE
    CUSTOM_MATRIX = yes
    SRC += keyboards/era/common/system/era_rp2040_matrix_core.c
    SRC += keyboards/era/common/system/era_matrix_debounce_runtime.c
    ifneq ($(strip $(ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY)),)
        OPT_DEFS += -DERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY=$(strip $(ERA_RP2040_MATRIX_GPIO_INPUT_PIN_DELAY))
    endif
    # The one raw backend, the PIO+DMA sampler, and the marker it is one fact
    # with. The sampler allocates its two DMA channels through the ChibiOS DMA
    # LLD -- the same allocator the ws2812 vendor driver uses, so the two
    # cannot be handed one channel -- and that LLD compiles only under
    # RP_DMA_REQUIRED, which platforms/chibios/vendors/RP/RP2040.mk emits for
    # WS2812_DRIVER=vendor alone. Emitting it here is what makes the four ERA
    # boards with no addressable LED link; the DMA vector slots in
    # era_vector_defaults.c already key on the same marker and step aside by
    # themselves. Not a selector (era_build_options.md, rule 5): the sampler
    # is what an ERA matrix engine build is, and its unit checks the marker
    # with an #error, so one line without the other fails the compile rather
    # than resolving to nothing. There is no off state and no second raw
    # backend to select; why not is era_build_options.md's.
    OPT_DEFS += -DRP_DMA_REQUIRED=TRUE
    SRC += keyboards/era/common/system/era_rp2040_matrix_pio.c
endif

ifeq ($(strip $(ERA_SOCD_ENABLE)), yes)
    OPT_DEFS += -DERA_SOCD_ENABLE
    SRC += keyboards/era/common/features/era_socd.c
    ERA_COMMON_VIA_SRCS += keyboards/era/common/features/era_socd_via.c
endif

ifeq ($(strip $(ERA_KKUK_ENABLE)), yes)
    OPT_DEFS += -DERA_KKUK_ENABLE
    SRC += keyboards/era/common/features/era_kkuk.c
    ERA_COMMON_VIA_SRCS += keyboards/era/common/features/era_kkuk_via.c
endif

ifeq ($(strip $(ERA_BACKLIGHT_EFFECT_ENABLE)), yes)
    # Refused in make rather than in C: the selector without the QMK feature
    # links a unit whose every call is to a backlight symbol that is not there,
    # so the honest failure is one line before any compile rather than a page
    # of undefined references at the link.
    ifneq ($(strip $(BACKLIGHT_ENABLE)), yes)
        $(error ERA backlight effects require BACKLIGHT_ENABLE; they are a layer over QMK's backlight, not a driver)
    endif
    OPT_DEFS += -DERA_BACKLIGHT_EFFECT_ENABLE
    SRC += keyboards/era/common/features/era_backlight.c
    ERA_COMMON_VIA_SRCS += keyboards/era/common/features/era_backlight_via.c
endif

ifeq ($(strip $(ERA_BACKLIGHT_LOCK_ENABLE)), yes)
    ifneq ($(strip $(BACKLIGHT_ENABLE)), yes)
        $(error ERA_BACKLIGHT_LOCK_ENABLE is a rule about QMK's backlight; there is no backlight on this board to keep enabled)
    endif
    OPT_DEFS += -DERA_BACKLIGHT_LOCK_ENABLE
    SRC += keyboards/era/common/features/era_backlight_lock.c
endif

ifeq ($(strip $(ERA_RGB_INDICATOR_ENABLE)), yes)
    # Refused in make for era_backlight's reason: without the QMK feature this
    # unit's every call is to an RGB Matrix symbol that is not there, so one
    # line before any compile beats a page of undefined references at the link.
    ifneq ($(strip $(RGB_MATRIX_ENABLE)), yes)
        $(error ERA RGB indicators require RGB_MATRIX_ENABLE; they paint named LEDs on the matrix panel, they are not a driver)
    endif
    OPT_DEFS += -DERA_RGB_INDICATOR_ENABLE
    SRC += keyboards/era/common/features/era_rgb_indicator.c
    ERA_COMMON_VIA_SRCS += keyboards/era/common/features/era_rgb_indicator_via.c
endif

ifeq ($(strip $(ERA_DEBOUNCE_ENABLE)), yes)
    DEBOUNCE_TYPE = custom
    OPT_DEFS += -DERA_DEBOUNCE_ENABLE
    ifneq ($(strip $(ERA_RP2040_MATRIX_ENABLE)), yes)
        SRC += keyboards/era/common/system/era_matrix_debounce_runtime.c
    endif
    SRC += keyboards/era/common/system/era_matrix_debounce_config.c
    ERA_COMMON_VIA_SRCS += keyboards/era/common/features/era_debounce_via.c
endif

ifeq ($(strip $(ERA_TAPPING_CONFIG_ENABLE)), yes)
    OPT_DEFS += -DERA_TAPPING_CONFIG_ENABLE
    SRC += keyboards/era/common/features/era_tapping.c
    ERA_COMMON_VIA_SRCS += keyboards/era/common/features/era_tapping_via.c
endif

ifeq ($(strip $(ERA_MOUSEKEY_CONFIG_ENABLE)), yes)
    # Refused in make for era_backlight's reason: the unit assigns to the mouse
    # engine's runtime variables, which quantum/mousekey.c defines and a build
    # without the feature never compiles, so one line before any compile beats
    # ten undefined references at the link.
    ifneq ($(strip $(MOUSEKEY_ENABLE)), yes)
        $(error ERA mouse key tuning requires MOUSEKEY_ENABLE; it is a VIA page over QMK's own mouse engine, not an engine)
    endif
    OPT_DEFS += -DERA_MOUSEKEY_CONFIG_ENABLE
    # Not a second selector (era_build_options.md, rule 5): the two per-event
    # step sizes are macros upstream and variables here, and this page is the
    # only thing that writes them, so the -D is one fact with the SRC line
    # beside it. quantum/mousekey.c keeps the macros without it and every
    # keyboard outside this layer pays no RAM (era_qmk_fork_ledger.md).
    OPT_DEFS += -DERA_MOUSEKEY_RUNTIME_DELTA
    SRC += keyboards/era/common/features/era_mousekey.c
    ERA_COMMON_VIA_SRCS += keyboards/era/common/features/era_mousekey_via.c
endif

# The ERA VIA surface, and nothing else, lives under this gate. A build without
# VIA has no command to route, so linking the router and its feature adapters
# into one was dead weight that only `gc-sections` was removing. The count is
# deliberately not written: it read "five" from the day it was written until
# era_nkro_via.c landed the next day, and nothing about the gate turns on it.
# The gate is possible
# at all because this file runs in the post_rules.mk phase, where VIA_ENABLE is
# already set (era_source_map.md's Build Selectors section); in the rules.mk
# phase it would have been silently false and compiled the surface away from
# every build.
#
# The closure is exact rather than approximate, which is what makes the gate
# safe: every export of every unit below is called only from era_common_via.c
# or from era_split_keyboard_handle_via_command(), and that function is reached
# only from each board's via_custom_value_command_kb(), which is already inside
# `#ifdef VIA_ENABLE` in all five board files. ERA_VIA_BOOTLOADER_ENABLE and
# ERA_EEPROM_CLEAN_ENABLE are read in era_via_system.c and nowhere else, so
# their -D belongs under the same gate rather than beside it -- emitting a
# selector whose only reader is not compiled is how a switch comes to look live
# when it is not.
ifeq ($(strip $(VIA_ENABLE)), yes)
    SRC += keyboards/era/common/system/era_common_via.c
    SRC += keyboards/era/common/system/era_firmware_version.c
    SRC += keyboards/era/common/system/era_state_sync.c
    SRC += $(ERA_COMMON_VIA_SRCS)

    ifneq ($(filter yes,$(strip $(ERA_VIA_BOOTLOADER_ENABLE)) $(strip $(ERA_EEPROM_CLEAN_ENABLE))),)
        OPT_DEFS += -DERA_VIA_SYSTEM_ENABLE
        SRC += keyboards/era/common/system/era_via_system.c
    endif

    ifeq ($(strip $(ERA_VIA_BOOTLOADER_ENABLE)), yes)
        OPT_DEFS += -DERA_VIA_BOOTLOADER_ENABLE
    endif

    ifeq ($(strip $(ERA_EEPROM_CLEAN_ENABLE)), yes)
        OPT_DEFS += -DERA_EEPROM_CLEAN_ENABLE
    endif

    # Derived from QMK's own switch rather than offered beside it, the way the
    # tap-dance surface is: the toggle writes `keymap_config.nkro`, which a
    # board built with NKRO_ENABLE=no has no use for, and every board that has
    # the bit has the same bit. So there is nothing here for a board to choose
    # and no selector to get wrong -- a board turns the toggle off by turning
    # NKRO off, which is the only answer that is true of the firmware too.
    ifeq ($(strip $(NKRO_ENABLE)), yes)
        SRC += keyboards/era/common/features/era_nkro_via.c
        OPT_DEFS += -DERA_NKRO_VIA_ENABLE
    endif
endif
