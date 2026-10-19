// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ERA_LIGHTING_SLEEP_DEFAULT_SECONDS 600U

/* RGB and backlight sleep, one set of controls per output. The RGB switch is
 * the inverted keymap_config bit (era_rgb_sleep.h); the timeouts and the
 * backlight switch share one record in the ERA block, where a zeroed record
 * reads as both timeouts at the default and backlight sleep on. */
void     era_lighting_sleep_init(void);
void     era_lighting_sleep_reload_from_eeprom(void);
uint16_t era_lighting_sleep_rgb_timeout_seconds(void);
bool     era_lighting_sleep_set_rgb_timeout_seconds(uint16_t seconds);
bool     era_lighting_sleep_backlight_enabled(void);
bool     era_lighting_sleep_set_backlight_enabled(bool enabled);
uint16_t era_lighting_sleep_backlight_timeout_seconds(void);
bool     era_lighting_sleep_set_backlight_timeout_seconds(uint16_t seconds);

#ifndef SPLIT_KEYBOARD
/* The non-split class: decide RGB and backlight sleep from the USB session
 * facts and the input-idle time, and drive each output gate. Idempotent, so
 * the 1 ms session task and the suspend hooks can all call it. The split class
 * resolves RGB sleep through its relation owner instead. */
void era_lighting_sleep_update(bool explicit_suspend, bool frames_lost, uint32_t matrix_idle_ms);
#endif

#ifdef VIA_ENABLE
bool era_lighting_sleep_handle_via_command(uint8_t *data, uint8_t length);
#endif
