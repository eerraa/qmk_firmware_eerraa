# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic host proof of the production Backlight Pulse engine. The TEST
# platform supplies a tiny ChibiOS virtual-timer shim; PWM and ERA EEPROM are
# in-memory stubs in the test so the real effect state machine is exercised.

SRC += keyboards/era/common/features/era_backlight.c
SRC += keyboards/era/common/features/era_backlight_lock.c

BACKLIGHT_ENABLE = yes
BACKLIGHT_DRIVER = custom

OPT_DEFS += -DPROTOCOL_CHIBIOS -DBACKLIGHT_BREATHING -DBACKLIGHT_LEVELS=10 -DBREATHING_PERIOD=5
OPT_DEFS += -DERA_BACKLIGHT_EFFECT_ENABLE -DERA_BACKLIGHT_LOCK_ENABLE
