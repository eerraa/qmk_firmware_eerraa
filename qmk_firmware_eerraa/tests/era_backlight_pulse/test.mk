# Copyright 2026 Hyojin Bak (@eerraa)
# SPDX-License-Identifier: GPL-2.0-or-later

# Deterministic host proof of the production Backlight Pulse engine. The TEST
# platform supplies a tiny ChibiOS virtual-timer shim; PWM and ERA EEPROM are
# in-memory stubs in the test so the real effect state machine is exercised.

SRC += keyboards/era/common/features/era_backlight.c
SRC += keyboards/era/common/features/era_backlight_lock.c
SRC += keyboards/era/common/features/era_tapdance.c
SRC += keyboards/era/common/features/era_tapping.c
OPT_DEFS += -DTAPPING_TERM_PER_KEY
# Retro tapping is the product VIA option, default off; TD cases enable it.
OPT_DEFS += -DRETRO_TAPPING_PER_KEY
# The split cross-half judgment unit, as every ERA split build links it. Its
# windows arm only from the tapping options and act only on a peer cache.
SRC += keyboards/era/common/split/era_split_tap_activity.c
SRC += tests/era_backlight_pulse/test_split_activity.c
OPT_DEFS += -DERA_SPLIT_TAP_ACTIVITY_ENABLE
SRC += keyboards/era/common/system/era_common_features.c
# SOCD resolves on the outgoing report, so dance outputs are resolved too.
SRC += keyboards/era/common/features/era_socd.c
OPT_DEFS += -DERA_SOCD_ENABLE

BACKLIGHT_ENABLE = yes
BACKLIGHT_DRIVER = custom
TAP_DANCE_ENABLE = yes
# Keep the ordinary 6KRO fixture default; explicit cases also exercise NKRO.
NKRO_ENABLE = yes
# Mouse codes and system/consumer usages share TD output ownership.
MOUSEKEY_ENABLE = yes
EXTRAKEY_ENABLE = yes
INTROSPECTION_KEYMAP_C = tap_dance_defs.c

OPT_DEFS += -DPROTOCOL_CHIBIOS -DBACKLIGHT_BREATHING -DBACKLIGHT_LEVELS=10 -DBREATHING_PERIOD=5
OPT_DEFS += -DERA_BACKLIGHT_EFFECT_ENABLE -DERA_BACKLIGHT_LOCK_ENABLE
OPT_DEFS += -DERA_TAP_DANCE_ENABLE -DERA_TAP_DANCE_KEYCODE_BASE=0x5700

# Production LED feedback path with a test-only electrical pin adapter.
SRC += tests/era_backlight_pulse/test_caps_feedback_led.c
