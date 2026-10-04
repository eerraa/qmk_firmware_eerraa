// SPDX-License-Identifier: GPL-2.0-or-later
// Only physical storage, MCU reset, clocks and optional board payloads are modeled.
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "eeprom.h"
#include "eeprom_driver.h"
#include "dynamic_keymap.h"
#include "wear_leveling.h"
#include "wear_leveling_internal.h"
#include "keyboards/era/common/system/era_state_sync.h"
#include "keyboards/era/common/system/era_via_system.h"
#include "keyboards/era/common/storage/era_eeprom_config_io.h"
static uint16_t physical[WEAR_LEVELING_BACKING_SIZE / 2];
static unsigned fault, resets, programs, erases;
static bool fail_erase;
bool backing_store_init(void) { return true; }
bool backing_store_unlock(void) { return true; }
bool backing_store_lock(void) { return true; }
bool backing_store_erase(void) { erases++; if (fail_erase) return false; memset(physical, 0, sizeof(physical)); return true; }
bool backing_store_read(uint32_t a, backing_store_int_t *v) { assert(a % 2 == 0 && a + 2 <= sizeof(physical)); *v = physical[a / 2]; return true; }
bool backing_store_write(uint32_t a, backing_store_int_t v) {
    programs++;
    assert(a % 2 == 0 && a + 2 <= sizeof(physical));
    if (fault == 1) return false;
    physical[a / 2] |= fault == 2 ? v & 0xFFU : v;
    return fault == 0 && physical[a / 2] == v;
}
void raw_hid_send(uint8_t *data, uint8_t length) { (void)data; (void)length; }
#define ERA_EEPROM_CLEAN_ENABLE
#define ERA_STORAGE_QUIET_DEFER_MS 500
#define EECONFIG_MAGIC ((uint16_t *)0)
#define EECONFIG_MAGIC_NUMBER_OFF 0xFFFFU
#define ERA_VIA_SYSTEM_RESTART_HID_QUIET_MS 500U
#define ERA_VIA_SYSTEM_EEPROM_RESET_MASK 7U
static bool era_board_keyboard_save_pending, eeprom_reset_pending;
static uint8_t eeprom_reset_confirm;
static uint16_t era_board_keyboard_save_timer;
static uint32_t eeprom_reset_requested_ms, eeprom_reset_retry_started_ms, now_ms, via_last_raw_hid_ms;
static uint16_t eeprom_reset_retry_delay_ms;
static uint16_t timer_read(void) { return 0; }
static uint32_t timer_read32(void) { return now_ms; }
static uint32_t timer_elapsed32(uint32_t start) { return now_ms - start; }
static void era_common_via_keyboard_channel_save(void) { eeprom_update_byte((uint8_t *)91, 0x69); }
static void era_board_via_save(void) {}
static bool shutdown_user(bool boot) { (void)boot; return true; }
static bool shutdown_modules(bool boot) { (void)boot; return true; }
static void clear_keyboard(void) {}
static void mcu_reset(void) { resets++; }
/* PRODUCTION_FUNCTIONS */

int main(void) {
    for (unsigned pending = 0; pending < 2; pending++) {
        for (unsigned mode = 1; mode <= 3; mode++) {
            memset(physical, 0, sizeof(physical)); fault = 0; fail_erase = false; resets = 0;
            eeprom_driver_init(); eeprom_update_word(EECONFIG_MAGIC, 0xFEE7); eeprom_update_byte((uint8_t *)91, 0x5A);
            assert(eeprom_persistence_ok());
            era_board_keyboard_save_pending = pending;
            now_ms = 3000; via_last_raw_hid_ms = eeprom_reset_requested_ms = 0;
            eeprom_reset_retry_delay_ms = 0;
            fault = mode; eeprom_reset_pending = true;
            era_via_system_task();
            assert(resets == 0 && eeprom_reset_pending && !eeprom_persistence_ok());
            assert(era_board_keyboard_save_pending == (pending != 0));
            unsigned before_programs = programs, before_erases = erases;
            for (unsigned pass = 0; pass < 100; pass++) era_via_system_task();
            now_ms += 999;
            era_via_system_task();
            assert(programs == before_programs && erases == before_erases);
            fault = 0; fail_erase = true;
            now_ms++;
            era_via_system_task();
            assert(resets == 0 && eeprom_reset_pending && !eeprom_persistence_ok());
            assert(erases == before_erases + 1);
            fail_erase = false;
            now_ms += 2000;
            era_via_system_task();
            assert(resets == 1 && !eeprom_reset_pending && eeprom_persistence_ok());
            eeprom_driver_init();
            assert(eeprom_read_word(EECONFIG_MAGIC) == 0xFFFF);
            assert(eeprom_read_byte((uint8_t *)91) == (pending ? 0x69 : 0x5A));
        }
    }
    puts("PASS WL CLEAN no-effect/torn/ambiguous program and erase failure; checked retry, shutdown approvals, cold invalid magic");
    memset(physical, 0, sizeof(physical)); fault = 0; resets = 0;
    eeprom_driver_init(); eeprom_update_word(EECONFIG_MAGIC, 0xFEE7);
    eeprom_reset_pending = false; eeprom_reset_confirm = ERA_VIA_SYSTEM_EEPROM_RESET_MASK;
    now_ms = UINT32_MAX - 499U; via_last_raw_hid_ms = now_ms;
    era_via_system_trigger_eeprom_reset_if_confirmed();
    unsigned before_programs = programs, before_erases = erases;
    era_via_system_task();
    assert(programs == before_programs && erases == before_erases && resets == 0);
    now_ms += 500U; // first eligible pass crosses timer wrap
    fault = 1;
    era_via_system_task();
    const uint16_t intervals[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
    for (unsigned i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
        assert(eeprom_reset_pending && resets == 0);
        before_programs = programs; before_erases = erases;
        uint32_t retry_started = eeprom_reset_retry_started_ms, requested = eeprom_reset_requested_ms;
        eeprom_reset_confirm = ERA_VIA_SYSTEM_EEPROM_RESET_MASK;
        era_via_system_trigger_eeprom_reset_if_confirmed();
        assert(eeprom_reset_retry_started_ms == retry_started && eeprom_reset_requested_ms == requested);
        now_ms += intervals[i] - 1U;
        for (unsigned pass = 0; pass < 100; pass++) era_via_system_task();
        assert(programs == before_programs && erases == before_erases);
        now_ms++;
        era_via_system_task();
        assert(programs == before_programs + 1 && erases == before_erases + 1);
    }
    fault = 0; now_ms += 30000U; era_via_system_task();
    assert(resets == 1 && !eeprom_reset_pending && eeprom_persistence_ok());
    before_programs = programs; before_erases = erases;
    era_via_system_task();
    assert(programs == before_programs && erases == before_erases);
    eeprom_driver_init(); assert(eeprom_read_word(EECONFIG_MAGIC) == 0xFFFF);
    puts("PASS CLEAN quiet gate, bounded exponential retries, duplicate requests, timer wrap and one recovery reset");
    uint8_t packet[28]; memset(packet, 0x5A, sizeof(packet));
    era_state_sync_set_revisions_for_testing(1, 1, 1);
    fault = 1; eeprom_update_block(packet, (void *)321, sizeof(packet));
    assert(!eeprom_persistence_ok() && era_state_sync_keymap_revision() == 1);
    fault = 0; eeprom_update_block(packet, (void *)321, sizeof(packet));
    assert(eeprom_persistence_ok() && era_state_sync_keymap_revision() == 2);
    eeprom_driver_init(); uint8_t readback[28]; eeprom_read_block(readback, (void *)321, sizeof(readback));
    assert(!memcmp(packet, readback, sizeof(packet)));
    era_state_sync_set_revisions_for_testing(1, 1, 1);
    uint8_t config = 0x42;
    era_state_sync_note_config_semantic_commit(0, 1);
    fault = 1; assert(era_eeprom_update_config(&config, 0, 1) == 0);
    fault = 0; eeprom_update_byte((uint8_t *)350, 0x66);
    assert(eeprom_persistence_ok() && era_state_sync_keymap_revision() == 2 && era_state_sync_config_revision() == 2);
    eeprom_update_byte((uint8_t *)37, 0x43);
    assert(era_state_sync_config_revision() == 3); // recovered debt is consumed
    uint8_t opened = 0xFF, closed = 0;
    nvm_dynamic_keymap_macro_update_buffer(127, 1, &opened);
    nvm_dynamic_keymap_macro_update_buffer(0, sizeof(packet), packet);
    fault = 1; nvm_dynamic_keymap_macro_update_buffer(127, 1, &closed);
    assert(!eeprom_persistence_ok() && era_state_sync_macro_revision() == 1);
    fault = 0; nvm_dynamic_keymap_macro_update_buffer(127, 1, &closed);
    assert(eeprom_persistence_ok() && era_state_sync_macro_revision() == 2);
    puts("PASS one KEYMAP/MACRO revision per recovered completion");
}
