// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"

void era_common_features_init(void);
void era_common_features_reload_from_eeprom(void);
void era_common_features_task(void);
/* Opportunistic physical-NVM housekeeping, scheduled after the board's
 * presentation tick by both ERA class skeletons. */
void era_common_features_maintenance_task(void);

#if defined(ERA_BACKLIGHT_EFFECT_ENABLE) || defined(ERA_RGBLIGHT_PULSE_ENABLE)
/* Electrical matrix-key feedback, from QMK's switch-event fanout, for the
   keypress-reactive Pulse of either lighting family. */
void era_common_features_switch_event(uint8_t row, uint8_t col, bool pressed);
#endif

bool era_common_features_process_record(uint16_t keycode, keyrecord_t *record);
