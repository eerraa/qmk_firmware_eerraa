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

__attribute__((weak)) bool eeprom_driver_read_block_kb(uint32_t address, void *buf, size_t len) {
    (void)address;
    (void)buf;
    (void)len;
    return false;
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
    if (eeprom_driver_read_block_kb((uint32_t)(uintptr_t)addr, buf, len)) {
        return;
    }
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

#if defined(ERA_HOST_PEER_STORAGE_V1_ENABLE)
bool eeprom_driver_is_healthy(void) {
    return wear_leveling_is_healthy();
}

bool eeprom_driver_read_block_raw(void *buf, const void *addr, size_t len) {
    /* Rollback's old-public facade still needs the complete RAM image while a
     * failed whole-store repair is pending. Durability decisions ask
     * eeprom_driver_is_healthy() separately. */
    return wear_leveling_read((uint32_t)(uintptr_t)addr, buf, len) != WEAR_LEVELING_FAILED;
}

bool eeprom_driver_write_block_raw_checked(const void *buf, void *addr, size_t len) {
    uint32_t address = (uint32_t)(uintptr_t)addr;
    if (!eeprom_driver_write_begin_kb(address, len)) {
        return false;
    }
    wear_leveling_status_t status = wear_leveling_write(address, buf, len);
    eeprom_driver_write_end_kb(address, len);
    return status != WEAR_LEVELING_FAILED;
}

bool eeprom_driver_write_word_reboot_checked(uint16_t value, void *addr) {
    uint32_t address = (uint32_t)(uintptr_t)addr;
    if (!eeprom_driver_write_begin_kb(address, sizeof(value))) {
        return false;
    }
    wear_leveling_status_t status = wear_leveling_write_word_reboot_checked(address, value);
    eeprom_driver_write_end_kb(address, sizeof(value));
    return status != WEAR_LEVELING_FAILED;
}
#endif

#if defined(ERA_DYNAMIC_MACRO_TRANSACTION_ENABLE)
bool eeprom_driver_write_block_cache_only(const void *buf, void *addr, size_t len) {
    return wear_leveling_write_cache((uint32_t)(uintptr_t)addr, buf, len) != WEAR_LEVELING_FAILED;
}

bool eeprom_driver_commit_cache(void) {
    // The semantic change belongs to the dynamic-macro domain, but the
    // physical operation rewrites the whole consolidated wear-leveling image.
    // Bracket that actual span once so ERA's flash guard, responder park, and
    // diagnostics describe flash work rather than the preceding RAM writes.
    if (!eeprom_driver_write_begin_kb(0, TOTAL_EEPROM_BYTE_COUNT)) {
        return false;
    }
    wear_leveling_status_t status = wear_leveling_commit_cache();
    eeprom_driver_write_end_kb(0, TOTAL_EEPROM_BYTE_COUNT);
    return status != WEAR_LEVELING_FAILED;
}
#endif
