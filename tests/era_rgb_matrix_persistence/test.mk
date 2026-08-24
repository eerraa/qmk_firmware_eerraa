# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Focused host proof of the production RGB Matrix quiet-persistence helper.
# The real rgb_matrix.c is linked; only its synchronous eeconfig update is
# wrapped so a physical RM_TOGG-style re-arm can be injected inside the write.

RGB_MATRIX_ENABLE = yes
RGB_MATRIX_DRIVER = custom
OPT_DEFS += -DERA_STORAGE_QUIET_DEFER_MS=500 -DRGB_MATRIX_LED_COUNT=1
LDFLAGS += -Wl,--wrap=eeconfig_update_rgb_matrix
