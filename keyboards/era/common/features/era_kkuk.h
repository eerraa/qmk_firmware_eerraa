// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

enum {
    ERA_KKUK_MODE_REPORT_PULSE = 1,
};

void era_kkuk_init(void);
void era_kkuk_reload_from_eeprom(void);
void era_kkuk_task(void);
bool era_kkuk_process_record(uint16_t keycode, keyrecord_t *record);
void era_kkuk_save_config(void);
void era_kkuk_set_enabled(bool enabled);
void era_kkuk_set_delay_ticks(uint8_t delay_ticks);
void era_kkuk_set_repeat_ticks(uint8_t repeat_ticks);
void era_kkuk_set_mode(uint8_t mode);
bool era_kkuk_get_enabled(void);
uint8_t era_kkuk_get_delay_ticks(void);
uint8_t era_kkuk_get_repeat_ticks(void);
uint8_t era_kkuk_get_mode(void);
