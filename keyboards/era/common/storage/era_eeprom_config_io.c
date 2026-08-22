// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_eeprom_config_io.h"

#include "eeprom.h"
#include "era_eeprom_layout.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "platforms/eeprom.h"
#include "util.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

_Static_assert(ERA_EEPROM_CONFIG_END <= TOTAL_EEPROM_BYTE_COUNT, "ERA EEPROM storage exceeds logical EEPROM size.");

uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t length) {
    void *ee_start = (void *)(uintptr_t)(ERA_EEPROM_CONFIG_ADDR + offset);
    void *ee_end   = (void *)(uintptr_t)(ERA_EEPROM_CONFIG_ADDR + MIN(ERA_EEPROM_CONFIG_SIZE, offset + length));
    eeprom_read_block(buf, ee_start, ee_end - ee_start);
    return ee_end - ee_start;
}

uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t length) {
    void *ee_start = (void *)(uintptr_t)(ERA_EEPROM_CONFIG_ADDR + offset);
    void *ee_end   = (void *)(uintptr_t)(ERA_EEPROM_CONFIG_ADDR + MIN(ERA_EEPROM_CONFIG_SIZE, offset + length));
    uint16_t changed_offset = 0;
    uint16_t changed_length = 0;
#ifdef VIA_ENABLE
    era_state_sync_config_persist_begin((uint16_t)offset, (uint16_t)(ee_end - ee_start));
#endif
    nvm_eeprom_update_changed_block(buf, ee_start, (uint16_t)(ee_end - ee_start), &changed_offset, &changed_length);
    if (changed_length > 0) {
        nvm_eeprom_changed_kb(ERA_EEPROM_CONFIG_ADDR + offset + changed_offset, changed_length);
    }
#ifdef VIA_ENABLE
    era_state_sync_config_persist_end();
#endif
    return ee_end - ee_start;
}
