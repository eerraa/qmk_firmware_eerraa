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

#include <stdint.h>
#include <string.h>

#include "eeprom_driver.h"

__attribute__((weak)) bool eeprom_driver_can_skip_write(uint32_t address, size_t length) { (void)address; (void)length; return true; }
__attribute__((weak)) void eeprom_driver_note_write_start(void) {}
__attribute__((weak)) void eeprom_driver_note_write_result(uint32_t address, size_t length, bool changed, bool success) { (void)address; (void)length; (void)changed; (void)success; }
__attribute__((weak)) void eeprom_driver_reset_write_observer(void) {}
__attribute__((weak)) void nvm_dynamic_keymap_macro_reset_session(void) {}

uint8_t eeprom_read_byte(const uint8_t *addr) {
    uint8_t ret = 0;
    eeprom_read_block(&ret, addr, 1);
    return ret;
}

uint16_t eeprom_read_word(const uint16_t *addr) {
    uint16_t ret = 0;
    eeprom_read_block(&ret, addr, 2);
    return ret;
}

uint32_t eeprom_read_dword(const uint32_t *addr) {
    uint32_t ret = 0;
    eeprom_read_block(&ret, addr, 4);
    return ret;
}

void eeprom_write_byte(uint8_t *addr, uint8_t value) {
    eeprom_write_block(&value, addr, 1);
}

void eeprom_write_word(uint16_t *addr, uint16_t value) {
    eeprom_write_block(&value, addr, 2);
}

void eeprom_write_dword(uint32_t *addr, uint32_t value) {
    eeprom_write_block(&value, addr, 4);
}

void eeprom_update_block(const void *buf, void *addr, size_t len) {
    uint8_t read_buf[16];
    bool changed = !eeprom_driver_can_skip_write((uint32_t)(uintptr_t)addr, len);
    for (size_t offset = 0; offset < len; offset += sizeof(read_buf)) {
        size_t chunk = len - offset < sizeof(read_buf) ? len - offset : sizeof(read_buf);
        eeprom_read_block(read_buf, (const uint8_t *)addr + offset, chunk);
        changed |= memcmp((const uint8_t *)buf + offset, read_buf, chunk) != 0;
    }
    if (changed || !eeprom_driver_can_skip_write((uint32_t)(uintptr_t)addr, len)) eeprom_write_block(buf, addr, len);
}

void eeprom_update_byte(uint8_t *addr, uint8_t value) {
    uint8_t orig = eeprom_read_byte(addr);
    if (orig != value || !eeprom_driver_can_skip_write((uint32_t)(uintptr_t)addr, 1)) {
        eeprom_write_byte(addr, value);
    }
}

void eeprom_update_word(uint16_t *addr, uint16_t value) {
    uint16_t orig = eeprom_read_word(addr);
    if (orig != value || !eeprom_driver_can_skip_write((uint32_t)(uintptr_t)addr, 2)) {
        eeprom_write_word(addr, value);
    }
}

void eeprom_update_dword(uint32_t *addr, uint32_t value) {
    uint32_t orig = eeprom_read_dword(addr);
    if (orig != value || !eeprom_driver_can_skip_write((uint32_t)(uintptr_t)addr, 4)) {
        eeprom_write_dword(addr, value);
    }
}

void eeprom_driver_format(bool erase) __attribute__((weak));
void eeprom_driver_format(bool erase) {
    (void)erase; /* The default implementation assumes that the eeprom must be erased in order to be usable. */
    eeprom_driver_erase();
}
