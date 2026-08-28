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
bool eeprom_driver_read_block_kb(uint32_t address, void *buf, size_t len);
bool eeprom_driver_write_begin_kb(uint32_t address, size_t length);
void eeprom_driver_write_end_kb(uint32_t address, size_t length);

#if defined(ERA_HOST_PEER_STORAGE_V1_ENABLE) && defined(EEPROM_WEAR_LEVELING)
/* ERA replacement Apply's narrow internal seam. Public QMK EEPROM reads keep
 * their existing API and pass through read_block_kb(); the writer, verifier,
 * rollback, and runtime reload use these raw/cache operations deliberately. */
bool eeprom_driver_is_healthy(void);
bool eeprom_driver_read_block_raw(void *buf, const void *addr, size_t len);
bool eeprom_driver_write_block_raw_checked(const void *buf, void *addr, size_t len);
bool eeprom_driver_write_word_reboot_checked(uint16_t value, void *addr);
#endif

#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ENABLE) && (defined(EEPROM_WEAR_LEVELING) || defined(ERA_DYNAMIC_MACRO_TRANSACTION_TEST))
bool eeprom_driver_write_block_cache_only(const void *buf, void *addr, size_t len);
bool eeprom_driver_commit_cache(void);
#endif
