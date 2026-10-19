# SPDX-License-Identifier: GPL-2.0-or-later
SRC += keyboards/era/common/features/era_tapdance.c
SRC += keyboards/era/common/features/era_tapping.c
SRC += platforms/chibios/suspend.c
VPATH += tests/era_macro/hal_stubs
TAP_DANCE_ENABLE = yes
INTROSPECTION_KEYMAP_C = tap_dance_defs.c
OPT_DEFS += -DERA_TAP_DANCE_ENABLE -DERA_TAP_DANCE_KEYCODE_BASE=0x5700
OPT_DEFS += -DTAPPING_TERM_PER_KEY -DRETRO_TAPPING_PER_KEY
