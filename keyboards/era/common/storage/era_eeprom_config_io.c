// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_eeprom_config_io.h"

#include "eeprom.h"
#include "era_eeprom_layout.h"
#include "platforms/eeprom.h"
#include "util.h"
#ifdef VIA_ENABLE
#    include "../system/era_state_sync.h"
#endif

_Static_assert(ERA_EEPROM_CONFIG_END <= TOTAL_EEPROM_BYTE_COUNT, "ERA EEPROM storage exceeds logical EEPROM size.");

uint32_t era_eeprom_read_config(void *buf, uint32_t offset, uint32_t length) {
    if (offset >= ERA_EEPROM_CONFIG_SIZE || buf == NULL) return 0;
    length = MIN(length, ERA_EEPROM_CONFIG_SIZE - offset);
    eeprom_read_block(buf, (const void *)(uintptr_t)(ERA_EEPROM_CONFIG_ADDR + offset), length);
    return length;
}

uint32_t era_eeprom_update_config(const void *buf, uint32_t offset, uint32_t length) {
    if (offset >= ERA_EEPROM_CONFIG_SIZE || buf == NULL) return 0;
    length = MIN(length, ERA_EEPROM_CONFIG_SIZE - offset);
    if (!eeprom_begin_transaction()) return 0;
#ifdef VIA_ENABLE
    era_state_sync_config_persist_begin((uint16_t)offset, (uint16_t)length);
#endif
    eeprom_update_block(buf, (void *)(uintptr_t)(ERA_EEPROM_CONFIG_ADDR + offset), length);
    bool ok = eeprom_end_transaction();
#ifdef VIA_ENABLE
    era_state_sync_config_persist_end();
#endif
    return ok ? length : 0;
}
