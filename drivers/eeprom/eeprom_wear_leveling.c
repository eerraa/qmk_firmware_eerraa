// Copyright 2022 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdint.h>
#include <string.h>

#include "eeprom_driver.h"
#include "wear_leveling.h"
#include "eeprom.h"

static bool ready, write_ok, transaction_failed;
static uint16_t transaction_depth;
static uint32_t pending_first, pending_end;

bool eeprom_driver_can_skip_write(void) {
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
    pending_first = UINT32_MAX;
    pending_end = 0U;
}

void eeprom_driver_format(bool erase) {
    /* wear leveling requires the write log data structures to be erased before use. */
    (void)erase;
    eeprom_driver_erase();
}

void eeprom_driver_erase(void) {
    note_result(wear_leveling_erase());
}

void eeprom_read_block(void *buf, const void *addr, size_t len) {
    wear_leveling_read((uint32_t)(uintptr_t)addr, buf, len);
}

void eeprom_write_block(const void *buf, void *addr, size_t len) {
    if (!ready) { write_ok = false; return; }
    uint32_t address = (uint32_t)(uintptr_t)addr;
    if (len > WEAR_LEVELING_LOGICAL_SIZE || address > WEAR_LEVELING_LOGICAL_SIZE - len) { note_result(WEAR_LEVELING_FAILED); return; }
    bool repair = wear_leveling_is_pending();
    if (transaction_depth && transaction_failed) {
        (void)wear_leveling_stage(address, buf, len);
        write_ok = false;
    } else note_result(wear_leveling_write(address, buf, len));
    if (!write_ok && len != 0U) {
        if (address < pending_first) pending_first = address;
        if (address + len > pending_end) pending_end = address + len;
    }
    if (write_ok) {
        if (repair && pending_end > pending_first) eeprom_driver_note_commit(pending_first, pending_end - pending_first);
        pending_first = UINT32_MAX;
        pending_end = 0U;
        if (len != 0U) eeprom_driver_note_commit(address, len);
    }
}
