# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic host proof of the production Backlight Pulse engine. The TEST
# platform supplies a tiny ChibiOS virtual-timer shim; PWM and ERA EEPROM are
# in-memory stubs in the test so the real effect state machine is exercised.

SRC += keyboards/era/common/features/era_backlight.c
SRC += keyboards/era/common/features/era_backlight_lock.c
SRC += keyboards/era/common/features/era_tapdance.c
SRC += keyboards/era/common/system/era_common_features.c

BACKLIGHT_ENABLE = yes
BACKLIGHT_DRIVER = custom
TAP_DANCE_ENABLE = yes
INTROSPECTION_KEYMAP_C = tap_dance_defs.c

OPT_DEFS += -DPROTOCOL_CHIBIOS -DBACKLIGHT_BREATHING -DBACKLIGHT_LEVELS=10 -DBREATHING_PERIOD=5
OPT_DEFS += -DERA_BACKLIGHT_EFFECT_ENABLE -DERA_BACKLIGHT_LOCK_ENABLE
OPT_DEFS += -DERA_TAP_DANCE_ENABLE -DERA_TAP_DANCE_KEYCODE_BASE=0x5700
