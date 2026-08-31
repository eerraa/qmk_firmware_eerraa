// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

void    era_matrix_debounce_config_init(void);
void    era_matrix_debounce_config_reload_from_eeprom(void);
void    era_matrix_debounce_config_save(void);
bool    era_matrix_debounce_config_set_mode(uint8_t mode);
bool    era_matrix_debounce_config_set_single_delay(uint8_t delay_ms);
bool    era_matrix_debounce_config_set_press_delay(uint8_t delay_ms);
bool    era_matrix_debounce_config_set_release_delay(uint8_t delay_ms);
uint8_t era_matrix_debounce_config_get_mode(void);
uint8_t era_matrix_debounce_config_get_press_delay(void);
uint8_t era_matrix_debounce_config_get_release_delay(void);
