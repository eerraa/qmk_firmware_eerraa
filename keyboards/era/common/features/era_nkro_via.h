// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Value 5 on VIA's keyboard channel. The number is not free to change: it is
 * what every shipped ERA definition already addresses, and it was odessey's
 * board-local id before this surface became common. It sits below the tap
 * dance block (32..71) and above the 0..4 band, which is the one place on this
 * channel where two owners meet: a board's own handler keeps it (the tomak
 * family's badge lighting, odessey's indicators), except where
 * `ERA_BACKLIGHT_EFFECT_ENABLE` gives 0..3 to the common backlight adapter.
 * `era_identifier_map.md` is the whole allocation; 5 collides with neither. */
#define ERA_VIA_NKRO_ENABLE_VALUE_ID 5

bool era_nkro_via_is_value_id(uint8_t value_id);
bool era_nkro_via_handle_via_command(uint8_t *data, uint8_t length);
