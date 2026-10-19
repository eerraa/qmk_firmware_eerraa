# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Host proof of the production Core0/Core1 storage publication retirement.
# The wrapper includes the exact production C unit. Its 64-bit host translation
# removes compile-time assertions; the supported TOMAK build still owns them.

SRC += tests/era_split_storage_publication_retire/storage_under_test.c

INTROSPECTION_KEYMAP_C = keymap.c
OPT_DEFS += -Itests/era_split_storage_publication_retire/include
