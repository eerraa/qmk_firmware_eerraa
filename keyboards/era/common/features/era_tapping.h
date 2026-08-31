// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

void era_tapping_init(void);
void era_tapping_reload_from_eeprom(void);
void era_tapping_save_config(void);
void era_tapping_set_term_ms(uint16_t term_ms);
bool era_tapping_set_term_ms_exact(uint16_t term_ms);
void era_tapping_set_permissive_hold(bool enabled);
void era_tapping_set_hold_on_other_key_press(bool enabled);
void era_tapping_set_retro_tapping(bool enabled);
uint16_t era_tapping_get_term_ms(void);
bool era_tapping_get_permissive_hold(void);
bool era_tapping_get_hold_on_other_key_press(void);
bool era_tapping_get_retro_tapping(void);
