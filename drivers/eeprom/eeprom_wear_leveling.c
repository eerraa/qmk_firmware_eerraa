// Copyright 2022 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdint.h>
#include <string.h>

#include "eeprom_driver.h"
#include "wear_leveling.h"
#include "eeprom.h"

static bool ready, write_ok, transaction_failed;
static uint16_t transaction_depth;

bool eeprom_driver_can_skip_write(uint32_t address, size_t length) {
    (void)address; (void)length;
    return ready && write_ok && !wear_leveling_is_pending();
}
bool eeprom_begin_transaction(void) {
    if (!ready || transaction_depth == UINT16_MAX) return false;
    if (transaction_depth++ == 0U) { transaction_failed = false; write_ok = true; }
    return true;
}
bool eeprom_persistence_ok(void) {
    return ready && write_ok && (!transaction_depth || !transaction_failed) && !wear_leveling_is_pending();
}
bool eeprom_transaction_active(void) { return transaction_depth != 0U; }
void eeprom_reject_write(void) {
    write_ok = false;
    if (transaction_depth) transaction_failed = true;
}
bool eeprom_end_transaction(void) {
    if (transaction_depth == 0U) return false;
    --transaction_depth;
    bool ok = eeprom_persistence_ok();
    if (!transaction_depth) eeprom_transaction_complete(ok);
    return ok;
}
static void note_result(wear_leveling_status_t status) {
    write_ok = status != WEAR_LEVELING_FAILED;
    if (transaction_depth && !write_ok) transaction_failed = true;
}

void eeprom_driver_init(void) {
    ready = wear_leveling_init() != WEAR_LEVELING_FAILED;
    write_ok = ready;
    transaction_depth = 0U;
    transaction_failed = false;
    eeprom_driver_reset_write_observer();
    nvm_dynamic_keymap_macro_reset_session();
}

void eeprom_driver_format(bool erase) {
    /* wear leveling requires the write log data structures to be erased before use. */
    (void)erase;
    eeprom_driver_erase();
}

void eeprom_driver_erase(void) {
    note_result(wear_leveling_erase());
    if (write_ok) {
        eeprom_driver_reset_write_observer();
        nvm_dynamic_keymap_macro_reset_session();
    }
}

void eeprom_read_block(void *buf, const void *addr, size_t len) {
    wear_leveling_read((uint32_t)(uintptr_t)addr, buf, len);
}

static bool desired_bytes_changed(uint32_t address, const void *buf, size_t len) {
    uint8_t prior[16];
    for (size_t offset = 0; offset < len; offset += sizeof(prior)) {
        size_t chunk = len - offset < sizeof(prior) ? len - offset : sizeof(prior);
        wear_leveling_read(address + offset, prior, chunk);
        if (memcmp(prior, (const uint8_t *)buf + offset, chunk)) return true;
    }
    return false;
}

void eeprom_write_block(const void *buf, void *addr, size_t len) {
    if (!ready) { write_ok = false; return; }
    uint32_t address = (uint32_t)(uintptr_t)addr;
    if (len > WEAR_LEVELING_LOGICAL_SIZE || address > WEAR_LEVELING_LOGICAL_SIZE - len || (len && !buf)) { note_result(WEAR_LEVELING_FAILED); return; }
    bool changed = desired_bytes_changed(address, buf, len);
    if (transaction_depth && transaction_failed) {
        (void)wear_leveling_stage(address, buf, len);
        write_ok = false;
    } else note_result(wear_leveling_write(address, buf, len));
    eeprom_driver_note_write_result(address, len, changed, write_ok);
}
