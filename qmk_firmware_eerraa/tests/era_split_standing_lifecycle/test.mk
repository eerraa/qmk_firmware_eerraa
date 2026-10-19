# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
SRC += tests/era_split_standing_lifecycle/standing_under_test.c
INTROSPECTION_KEYMAP_C = keymap.c
LDFLAGS += -Wl,--gc-sections
OPT_DEFS += -Itests/era_split_standing_lifecycle/include

SRC += keyboards/era/common/split/era_split_input.c
SRC += tests/era_hid_report_interval/input_clock_stub.c
SRC += keyboards/era/common/split/era_split_matrix_frame.c
$(TEST_OBJ)/$(TEST_OUTPUT)/keyboards/era/common/split/era_split_input.o: FILE_SPECIFIC_CFLAGS += -DSPLIT_KEYBOARD
$(TEST_OBJ)/$(TEST_OUTPUT)/keyboards/era/common/split/era_split_matrix_frame.o: FILE_SPECIFIC_CFLAGS += -DSPLIT_KEYBOARD -include stddef.h
