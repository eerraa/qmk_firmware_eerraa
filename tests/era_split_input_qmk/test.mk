# SPDX-License-Identifier: GPL-2.0-or-later
SRC += keyboards/era/common/features/era_tapdance.c
SRC += keyboards/era/common/features/era_tapping.c
SRC += keyboards/era/common/system/era_hid_report_interval.c
TAP_DANCE_ENABLE = yes
NKRO_ENABLE = yes
INTROSPECTION_KEYMAP_C = tap_dance_defs.c
OPT_DEFS += -DERA_TAP_DANCE_ENABLE -DERA_TAP_DANCE_KEYCODE_BASE=0x5700
OPT_DEFS += -DTAPPING_TERM_PER_KEY -DRETRO_TAPPING_PER_KEY
OPT_DEFS += -DERA_SPLIT_INPUT_HISTORY_ENABLE
# Only tapping needs the half-geometry in this TEST-platform matrix fixture.
$(TEST_OBJ)/$(TEST_OUTPUT)/quantum/action_tapping.o: FILE_SPECIFIC_CFLAGS += -DSPLIT_KEYBOARD
