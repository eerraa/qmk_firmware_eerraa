/* Copyright 2019 Nick Brassel (tzarc)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdbool.h>
#include "eeprom.h"

void eeprom_driver_init(void);
void eeprom_driver_format(bool erase);
void eeprom_driver_erase(void);
/* Cache equality may skip a write only when its durable state is known. */
bool eeprom_driver_can_skip_write(uint32_t address, size_t length);
/* Observe each accepted logical write, including bytes retained after failure.
 * `changed` compares against desired RAM, so an equal repair is not a new edit.
 * Success also acknowledges the backend's previously retained desired image.
 * Consumers classify the actual spans; an enclosing physical envelope must not
 * stand in for writes that were never requested. */
void eeprom_driver_note_write_result(uint32_t address, size_t length, bool changed, bool success);
/* Init or a successful erase discards retained write intent. */
void eeprom_driver_reset_write_observer(void);
/* QMK's macro owner may keep RAM-only upload proof for this mounted image. */
void nvm_dynamic_keymap_macro_reset_session(void);
