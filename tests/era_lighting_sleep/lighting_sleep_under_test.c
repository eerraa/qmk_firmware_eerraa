// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

/* VIA and both outputs for this translation unit only, so the rest of the
   test image keeps QMK's defaults. */
#define VIA_ENABLE
#define ERA_RGB_SLEEP_MASTER_ENABLE
#define ERA_BACKLIGHT_SLEEP_ENABLE
#define ERA_LIGHTING_SLEEP_TEST
#include "keyboards/era/common/features/era_lighting_sleep.c"
