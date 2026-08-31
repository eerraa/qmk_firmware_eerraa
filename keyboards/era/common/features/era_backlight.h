// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

/* The PWM backlight effect layer: ERA's third lighting family.
 *
 * QMK's own backlight gives a level and a breathing toggle. This adds four
 * keypress-reactive Pulse effects and owns which of the six modes is selected.
 * It is common rather than per board because
 * what differs between backlight boards is the pin and the level count, which
 * are `keyboard.json`'s -- the behaviour is the same one
 * (`era_board_adoption.md`, the lighting families).
 *
 * **The four value ids are not chosen here.** They are keyboard-channel 0..3,
 * because that is what the shipped definitions already address, and a firmware
 * that answered somewhere else would be answering a question no VIA client
 * asks. That band is otherwise board-local (`era_identifier_map.md`), and this
 * feature and a board's own 0..4 handler are mutually exclusive by selector.
 */

enum era_backlight_effect {
    ERA_BACKLIGHT_EFFECT_STEADY               = 0,
    ERA_BACKLIGHT_EFFECT_BREATHING            = 1,
    ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS      = 2,
    ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS       = 3,
    ERA_BACKLIGHT_EFFECT_PULSE_OFF_PRESS_HOLD = 4,
    ERA_BACKLIGHT_EFFECT_PULSE_ON_PRESS_HOLD  = 5,
    ERA_BACKLIGHT_EFFECT_COUNT
};

enum {
    ERA_BACKLIGHT_PERIOD_MIN = 1,
    ERA_BACKLIGHT_PERIOD_MAX = 10,
    ERA_BACKLIGHT_SPEED_MIN  = 1,
    ERA_BACKLIGHT_SPEED_MAX  = 10,
};

void era_backlight_init(void);
void era_backlight_reload_from_eeprom(void);
void era_backlight_save_config(void);

/* Per matrix-scan pass. One `volatile bool` load and a branch in every effect,
   including while a blink is running -- see the unit's own header for why the
   clock is never read here. */
void era_backlight_task(void);

/* Per key event, never per pass. Always returns true: this feature observes
   the edge and consumes no keycode. */
bool era_backlight_process_record(uint16_t keycode, keyrecord_t *record);

/* Non-split lighting sleep enters through QMK's suspend hooks. These calls
 * cancel an in-flight pulse and keep timer callbacks from relighting the PWM
 * rail while USB/frame-loss policy owns the dark state. */
void era_backlight_suspend(void);
void era_backlight_resume(void);
void era_backlight_refresh_output(void);

uint8_t era_backlight_get_effect(void);
void    era_backlight_set_effect(uint8_t effect);
uint8_t era_backlight_get_breathing_period(void);
void    era_backlight_set_breathing_period(uint8_t period);
uint8_t era_backlight_get_pulse_speed(void);
void    era_backlight_set_pulse_speed(uint8_t speed);
