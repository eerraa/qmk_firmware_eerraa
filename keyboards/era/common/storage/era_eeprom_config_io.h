// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t length);
uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t length);
