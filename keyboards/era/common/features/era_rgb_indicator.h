// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

/* Lock indicators on named RGB Matrix LEDs: one or two slots, each a lock
 * source, a brightness and a colour, all configurable from VIA.
 *
 * Common rather than per board because what differs between the boards that
 * want it is *which LED index* each slot paints and how many slots there are,
 * and that is board geometry — `config.h`'s, under `era_build_options.md`'s
 * rule 3. The behaviour is one behaviour: four boards across two vendors
 * (`linx3/fave65s`, `linx3/n86`, `linx3/n87`, `sirind/brick65s`) had it as
 * hard-coded colours in two board `.c` files and as nothing at all in the other
 * two, which is the split `era_board_adoption.md`'s lighting-surface rule sends
 * to a common feature unit.
 *
 * **It is not the tomak or odessey indicator.** Those two are family content
 * and stay where they are: tomak paints a *badge range* on a split pair and
 * arbitrates against a STATUS field, odessey paints one RGBLIGHT LED outside
 * the underglow effect range. This one paints one LED per slot on a non-split
 * RGB Matrix panel and arbitrates against nothing, so folding the three
 * together would buy one unit carrying three products' rules.
 *
 * A board declares, in its own `config.h`:
 *
 *   ERA_RGB_INDICATOR_1_LED   the LED index slot 1 paints (required)
 *   ERA_RGB_INDICATOR_2_LED   the LED index slot 2 paints (optional; its
 *                             presence is what makes the board two-slot)
 *
 * What a slot does when it is *not* an indicator — master toggle off, or the
 * slot's own source set to Off — is that the LED goes back to the effect. One
 * rule, no special case, and it is the rule the owner stated for `n86`/`n87`:
 * the two LEDs move between an indicator role and an ordinary RGB Matrix one.
 */

enum era_rgb_indicator_source {
    ERA_RGB_INDICATOR_SOURCE_OFF = 0,
    ERA_RGB_INDICATOR_SOURCE_CAPS_LOCK,
    ERA_RGB_INDICATOR_SOURCE_SCROLL_LOCK,
    ERA_RGB_INDICATOR_SOURCE_NUM_LOCK,
    ERA_RGB_INDICATOR_SOURCE_COUNT
};

#if defined(ERA_RGB_INDICATOR_2_LED)
#    define ERA_RGB_INDICATOR_SLOT_COUNT 2
#else
#    define ERA_RGB_INDICATOR_SLOT_COUNT 1
#endif

void era_rgb_indicator_init(void);
void era_rgb_indicator_reload_from_eeprom(void);
void era_rgb_indicator_save_config(void);

/* The VIA surface's whole state access. `slot` is 0-based and a slot past
   ERA_RGB_INDICATOR_SLOT_COUNT is refused rather than clamped, so a definition
   that offers slot 2 on a one-slot board answers nothing instead of aliasing
   slot 1. */
bool    era_rgb_indicator_slot_exists(uint8_t slot);
bool    era_rgb_indicator_get_enabled(void);
void    era_rgb_indicator_set_enabled(bool enabled);
uint8_t era_rgb_indicator_get_source(uint8_t slot);
void    era_rgb_indicator_set_source(uint8_t slot, uint8_t source);
uint8_t era_rgb_indicator_get_brightness(uint8_t slot);
void    era_rgb_indicator_set_brightness(uint8_t slot, uint8_t brightness);
void    era_rgb_indicator_get_color(uint8_t slot, uint8_t *hue_sat);
void    era_rgb_indicator_set_color(uint8_t slot, const uint8_t *hue_sat);
