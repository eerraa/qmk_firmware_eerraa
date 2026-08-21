// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool era_tapdance_is_value_id(uint8_t value_id);
bool era_tapdance_handle_via_command(uint8_t *data, uint8_t length);
