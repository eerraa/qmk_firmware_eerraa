// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Values 0..3 on VIA's keyboard channel, and the numbers are not free to
 * change: they are what the shipped VIA definitions of the backlight boards
 * already address, carried across from the vendor's published v3 files.
 *
 *   0 brightness        range 0..BACKLIGHT_LEVELS
 *   1 effect            dropdown, `enum era_backlight_effect`
 *   2 breathing period  range 1..10, shown for effect 1
 *   3 blink speed       range 1..10, shown for effects 2 and 3
 *
 * The band 0..4 is otherwise where the tomak family and odessey keep their
 * board-local ids (`era_identifier_map.md`), and this router runs *ahead* of
 * the board hook. Nothing collides because no board has both a keyboard-channel
 * handler of its own and a PWM backlight, and `ERA_BACKLIGHT_EFFECT_ENABLE` is
 * what keeps that true: a board that grew both would have to choose.
 */
#define ERA_VIA_BACKLIGHT_BRIGHTNESS_VALUE_ID 0
#define ERA_VIA_BACKLIGHT_EFFECT_VALUE_ID 1
#define ERA_VIA_BACKLIGHT_BREATHING_PERIOD_VALUE_ID 2
#define ERA_VIA_BACKLIGHT_BLINK_SPEED_VALUE_ID 3

bool era_backlight_via_is_value_id(uint8_t value_id);
bool era_backlight_via_handle_via_command(uint8_t *data, uint8_t length);
void era_backlight_via_save(void);
