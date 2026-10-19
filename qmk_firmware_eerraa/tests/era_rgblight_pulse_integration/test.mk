# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later
# Link the production RGBLight mode, timer, colour and sleep paths. Only the
# physical LED driver and ChibiOS virtual timer are simulated.
RGBLIGHT_ENABLE = yes
RGBLIGHT_DRIVER = custom
SRC += keyboards/era/common/features/era_rgblight_pulse.c
SRC += keyboards/era/common/features/era_rgb_sleep.c
OPT_DEFS += -DPROTOCOL_CHIBIOS -DRGBLIGHT_LED_COUNT=4 -DRGBLIGHT_SLEEP
OPT_DEFS += -DERA_RGBLIGHT_PULSE_ENABLE -DERA_RGB_SLEEP_MASTER_ENABLE
OPT_DEFS += -DRGBLIGHT_LAYERS -DRGBLIGHT_MAX_LAYERS=1
