# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
SRC += tests/era_split_standing_lifecycle/standing_under_test.c
INTROSPECTION_KEYMAP_C = keymap.c
LDFLAGS += -Wl,--gc-sections
OPT_DEFS += -Itests/era_split_standing_lifecycle/include
