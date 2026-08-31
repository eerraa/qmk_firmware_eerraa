// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

/* The six controls the VIA page offers, in the units the page states rather
   than in the units the engine stores. The two cursor speeds are effective
   pixels per event -- what the user is actually choosing -- and the adapter
   derives `mk_max_speed` from them; every other control maps one to one. Which
   raw value each one moves is at the setter in era_mousekey.c. */
void era_mousekey_init(void);
void era_mousekey_reload_from_eeprom(void);
void era_mousekey_save_config(void);

void era_mousekey_set_cursor_min_speed(uint8_t pixels);
void era_mousekey_set_cursor_max_speed(uint8_t pixels);
/* The climb to the top speed, in 50 ms units, with zero meaning the
   constant-speed mode. A time and not a count of events, so the update rate
   below changes only how finely the motion is cut -- the conversion and what
   bounds it are at the setter. */
void era_mousekey_set_cursor_acceleration(uint8_t ramp_units);
void era_mousekey_set_cursor_interval_ms(uint8_t interval_ms);
void era_mousekey_set_wheel_interval_ms(uint8_t interval_ms);
void era_mousekey_set_wheel_acceleration(uint8_t level);

uint8_t era_mousekey_get_cursor_min_speed(void);
uint8_t era_mousekey_get_cursor_max_speed(void);
uint8_t era_mousekey_get_cursor_acceleration(void);
uint8_t era_mousekey_get_cursor_interval_ms(void);
uint8_t era_mousekey_get_wheel_interval_ms(void);
uint8_t era_mousekey_get_wheel_acceleration(void);
