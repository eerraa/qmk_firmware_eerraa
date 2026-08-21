// Copyright 2022 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdint.h>
#include <string.h>

#include "eeprom_driver.h"
#include "wear_leveling.h"

__attribute__((weak)) bool eeprom_driver_write_begin_kb(uint32_t address, size_t length) {
    (void)address;
    (void)length;
    return true;
}

__attribute__((weak)) void eeprom_driver_write_end_kb(uint32_t address, size_t length) {
    (void)address;
    (void)length;
}

void eeprom_driver_init(void) {
    wear_leveling_init();
}

void eeprom_driver_format(bool erase) {
    /* wear leveling requires the write log data structures to be erased before use. */
    (void)erase;
    eeprom_driver_erase();
}

void eeprom_driver_erase(void) {
    if (!eeprom_driver_write_begin_kb(0, TOTAL_EEPROM_BYTE_COUNT)) {
        return;
    }
    wear_leveling_erase();
    eeprom_driver_write_end_kb(0, TOTAL_EEPROM_BYTE_COUNT);
}

void eeprom_read_block(void *buf, const void *addr, size_t len) {
    wear_leveling_read((uint32_t)addr, buf, len);
}

void eeprom_write_block(const void *buf, void *addr, size_t len) {
    uint32_t address = (uint32_t)(uintptr_t)addr;
    if (!eeprom_driver_write_begin_kb(address, len)) {
        return;
    }
    wear_leveling_write(address, buf, len);
    eeprom_driver_write_end_kb(address, len);
}
