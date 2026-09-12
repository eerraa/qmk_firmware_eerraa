# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Reason/preset policy and the production relation-owned lighting resolver.
SRC += tests/era_split_rgb_sleep_policy/lighting_under_test.c
LDFLAGS += -Wl,--gc-sections
