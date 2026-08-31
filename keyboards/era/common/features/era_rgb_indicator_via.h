// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Values 6..12 on VIA's keyboard channel.
 *
 *   6  indicator enable   toggle, the master role switch
 *   7  indicator 1 source dropdown, `enum era_rgb_indicator_source`
 *   8  indicator 1 brightness  range 0..255
 *   9  indicator 1 colour      hue/sat
 *  10  indicator 2 source
 *  11  indicator 2 brightness
 *  12  indicator 2 colour
 *
 * **Above the band a board keeps for itself and above the NKRO toggle, not
 * inside either** (owner decision 2026-08-18, `era_identifier_map.md`). The
 * shape this feature was first drafted in extended odessey's `0..3` indicator
 * ids with a second slot at `4..6` and would have put indicator 2's brightness
 * on `5`, which is `ERA_VIA_NKRO_ENABLE_VALUE_ID` — and the common router runs
 * ahead of the board hook, so the slider would have toggled NKRO. Starting at 6
 * costs nothing and leaves `0..4` board-local on a board that later grows both.
 *
 * A one-slot board answers 6..9 and declines 10..12, which reaches
 * `id_unhandled` through the ordinary chain rather than aliasing slot 1.
 */
#define ERA_VIA_RGB_INDICATOR_FIRST_VALUE_ID 6
#define ERA_VIA_RGB_INDICATOR_ENABLE_VALUE_ID 6
#define ERA_VIA_RGB_INDICATOR_SLOT_BASE_VALUE_ID 7
#define ERA_VIA_RGB_INDICATOR_VALUES_PER_SLOT 3
#define ERA_VIA_RGB_INDICATOR_LAST_VALUE_ID 12

bool era_rgb_indicator_via_is_value_id(uint8_t value_id);
bool era_rgb_indicator_via_handle_via_command(uint8_t *data, uint8_t length);
void era_rgb_indicator_via_save(void);
