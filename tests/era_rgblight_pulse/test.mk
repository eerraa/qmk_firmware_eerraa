# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic host proof of the production underglow Pulse engine. The TEST
# platform supplies the same tiny ChibiOS virtual-timer shim as the backlight
# proof; RGBLight is stubbed at its public boundary so the test observes
# exactly what the engine asks the chain to show without driving WS2812 I/O.

SRC += keyboards/era/common/features/era_rgblight_pulse.c

# Expose RGBLight's public headers without pulling the device driver in.
VPATH += $(QUANTUM_DIR)/rgblight

OPT_DEFS += -DPROTOCOL_CHIBIOS -DRGBLIGHT_LED_COUNT=4 -DERA_RGBLIGHT_PULSE_ENABLE
