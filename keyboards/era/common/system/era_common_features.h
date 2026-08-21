// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "quantum.h"

void era_common_features_init(void);
void era_common_features_reload_from_eeprom(void);
void era_common_features_task(void);

bool era_common_features_process_record(uint16_t keycode, keyrecord_t *record);
