# SPDX-License-Identifier: GPL-2.0-or-later
SRC += keyboards/era/common/features/era_tapdance.c
SRC += keyboards/era/common/features/era_tapping.c
SRC += keyboards/era/common/system/era_hid_report_interval.c
SRC += tests/era_local_release_gate/boundary.c
TAP_DANCE_ENABLE = yes
NKRO_ENABLE = yes
INTROSPECTION_KEYMAP_C = tap_dance_defs.c
OPT_DEFS += -DERA_TAP_DANCE_ENABLE -DERA_TAP_DANCE_KEYCODE_BASE=0x5700
OPT_DEFS += -DTAPPING_TERM_PER_KEY -DRETRO_TAPPING_PER_KEY
OPT_DEFS += -DERA_SPLIT_INPUT_HISTORY_ENABLE -DERA_HID_REPORT_INTERVAL_ENABLE
OPT_DEFS += -Itests/era_local_release_gate/include -Itests/era_usb_session_policy/include
OPT_DEFS += -Iquantum/split_common
$(TEST_OBJ)/$(TEST_OUTPUT)/quantum/action_tapping.o: FILE_SPECIFIC_CFLAGS += -DSPLIT_KEYBOARD

# Keep the production split-only quantum_task gate and its shared-clock caller.
SRC += quantum/sync_timer.c
$(TEST_OBJ)/$(TEST_OUTPUT)/quantum/keyboard.o: FILE_SPECIFIC_CFLAGS += -DSPLIT_KEYBOARD
$(TEST_OBJ)/$(TEST_OUTPUT)/quantum/sync_timer.o: FILE_SPECIFIC_CFLAGS += -DSPLIT_KEYBOARD
# File-specific flags are not part of common cflags.txt. Rebuild these objects
# whenever this fixture changes its physical-half or split task configuration.
$(TEST_OBJ)/$(TEST_OUTPUT)/quantum/action_tapping.o $(TEST_OBJ)/$(TEST_OUTPUT)/quantum/keyboard.o $(TEST_OBJ)/$(TEST_OUTPUT)/quantum/sync_timer.o: tests/era_local_release_gate/test.mk
