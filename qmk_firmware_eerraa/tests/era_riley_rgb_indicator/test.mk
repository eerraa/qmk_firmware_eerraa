# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic host proof of Riley's production RGBLight-layer indicator unit.
# RGBLight itself is stubbed at the layer boundary so the test observes exactly
# which overlay each lock-policy transition requests without driving WS2812 I/O.

SRC += keyboards/era/comm/riley/riley_common.c

# Define RGBLIGHT_ENABLE without pulling the whole device driver into the host
# test; expose only its public headers and stub the renderer boundary below.
VPATH += $(QUANTUM_DIR)/rgblight

OPT_DEFS += -DRGBLIGHT_ENABLE -DRGBLIGHT_LAYERS -DRGBLIGHT_MAX_LAYERS=3 -DRGBLIGHT_LED_COUNT=3 -DRGBLIGHT_SLEEP
OPT_DEFS += -DVELOCIKEY_ENABLE -DVIA_ENABLE -DRILEY_RGB_INDICATOR_TEST
