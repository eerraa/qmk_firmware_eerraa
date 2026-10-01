// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>

/* Boot reset guard for ERA_EEPROM_RESET_KEY. check() must run before
 * quantum_init() reads the eeconfig magic and returns whether it scheduled the
 * full erase; write() must be the last write of eeconfig_init_kb(), so a reset
 * cut short by power loss runs again. */
bool era_reset_guard_check(void);
void era_reset_guard_write(void);
