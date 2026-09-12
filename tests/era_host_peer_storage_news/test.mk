# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Production Core0 storage/news and the real 160 ms indicator presenter.
SRC += tests/era_host_peer_storage_news/storage_under_test.c
INTROSPECTION_KEYMAP_C = keymap.c
# Discard unexercised hardware/storage entry points, never unresolved symbols
# on a path under test. QMK's host compiler already emits function sections.
LDFLAGS += -Wl,--gc-sections
OPT_DEFS += -Itests/era_host_peer_storage_news/include
OPT_DEFS += -Iquantum/nvm/eeprom -Iquantum/rgb_matrix -Iquantum/rgb_matrix/animations
