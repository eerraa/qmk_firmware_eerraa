// SPDX-License-Identifier: GPL-2.0-or-later
// Production EEPROM/WL/State Sync and complete SAVE functions; physical backing
// and the owners' payload arrays are modeled. This is not an electrical test.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eeprom.h"
#include "eeprom_driver.h"
#include "wear_leveling.h"
#include "wear_leveling_internal.h"
#include "keyboards/era/common/storage/era_eeprom_config_io.h"
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#include "keyboards/era/common/storage/era_storage_layout.h"
#include "keyboards/era/common/system/era_state_sync.h"

static uint16_t physical[WEAR_LEVELING_BACKING_SIZE / 2];
static unsigned fault, macro_session_resets;
static bool erase_fails;
bool backing_store_init(void) { return true; }
bool backing_store_unlock(void) { return fault != 4; }
bool backing_store_lock(void) { return true; }
bool backing_store_erase(void) {
    if (erase_fails) return false;
    memset(physical, 0, sizeof(physical));
    return true;
}
bool backing_store_read(uint32_t address, backing_store_int_t *value) {
    if (address % 2 || address + 2 > sizeof(physical)) abort();
    *value = physical[address / 2];
    return true;
}
bool backing_store_write(uint32_t address, backing_store_int_t value) {
    if (address % 2 || address + 2 > sizeof(physical)) abort();
    if (fault == 1) return false;
    physical[address / 2] |= fault == 2 ? value & 0xFFU : value;
    return fault == 0 && physical[address / 2] == value;
}
void raw_hid_send(uint8_t *data, uint8_t length) { (void)data; (void)length; }
void nvm_dynamic_keymap_macro_reset_session(void) { macro_session_resets++; }

static uint8_t tapdance_storage[ERA_EEPROM_TAP_DANCE_CONFIG_SIZE];
static uint8_t tapdance_timing[ERA_EEPROM_TAP_DANCE_TIMING_SIZE];
static uint8_t tapping_config[ERA_EEPROM_TAPPING_CONFIG_SIZE];
#include "production_saves.inc"
#include "state_under_test.inc"

static void check(bool ok, const char *name) {
    if (!ok) {
        fprintf(stderr, "WL_SEMANTIC_ASSERT_%s\n", name);
        exit(91);
    }
}
#define CHECK(condition, name) check((condition), name)

static void fresh(void) {
    memset(physical, 0, sizeof(physical));
    memset(tapdance_storage, 0, sizeof(tapdance_storage));
    memset(tapdance_timing, 0, sizeof(tapdance_timing));
    memset(tapping_config, 0, sizeof(tapping_config));
    fault = 0;
    erase_fails = false;
    macro_session_resets = 0;
    eeprom_driver_init();
    era_state_sync_set_revisions_for_testing(1, 1, 1);
}
static void semantic_td(uint8_t primary, uint8_t timing) {
    memset(tapdance_storage, primary, sizeof(tapdance_storage));
    memset(tapdance_timing, timing, sizeof(tapdance_timing));
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, sizeof(tapdance_storage));
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_TAP_DANCE_TIMING_OFFSET, sizeof(tapdance_timing));
}
static void semantic_tapping(uint8_t value) {
    memset(tapping_config, value, sizeof(tapping_config));
    era_state_sync_note_config_semantic_commit(ERA_EEPROM_TAPPING_CONFIG_OFFSET, sizeof(tapping_config));
}
static void cold_byte(uint32_t address, uint8_t value) {
    uint32_t keymap = era_state_sync_keymap_revision(), macro = era_state_sync_macro_revision(), config = era_state_sync_config_revision();
    eeprom_driver_init();
    CHECK(eeprom_read_byte((const uint8_t *)(uintptr_t)address) == value, "COLD_BYTES");
    CHECK(era_state_sync_keymap_revision() == keymap && era_state_sync_macro_revision() == macro && era_state_sync_config_revision() == config, "INIT_KEEPS_REVISIONS");
}

static void sparse_debt(void) {
    for (unsigned mode = 1; mode <= 5; ++mode) {
        fresh();
        semantic_td(0x45, 0x67);
        semantic_tapping(0x89);
        CHECK(s_config_pending_persist_mask == 0x1003U, "SET_OWNER_DEBT");
        fault = mode == 5 ? 1 : mode;
        CHECK(!era_tapdance_save_config(), "FAILED_TD_RECEIPT");
        CHECK(era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 0x1003U, "FAILED_SCOPE_KEEPS_DEBT");
        fault = 0;
        if (mode == 5) {
            erase_fails = true;
            eeprom_update_byte((uint8_t *)350, 0x55);
            CHECK(!eeprom_persistence_ok(), "FAILED_REPAIR_ERASE");
            erase_fails = false;
        }
        eeprom_update_byte((uint8_t *)350, 0x55);
        CHECK(eeprom_persistence_ok() && era_state_sync_keymap_revision() == 2, "KEYMAP_REPAIR_ONCE");
        CHECK(era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 2, "UNSAVED_GAP_DEBT");
        CHECK(eeprom_read_byte((uint8_t *)125) == 0, "GAP_NOT_SAVED");
        CHECK(era_tapping_save_config(), "TAPPING_SAVE");
        CHECK(era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 0, "MATCHING_SAVE_NO_DUPLICATE");
        cold_byte(125, 0x89);
    }
}

static void newer_set(void) {
    fresh();
    semantic_td(0x45, 0x67);
    fault = 1;
    CHECK(!era_tapdance_save_config(), "OLDER_SAVE_FAILS");
    memset(tapdance_storage, 0x46, sizeof(tapdance_storage));
    era_state_sync_note_config_semantic_commit(0, sizeof(tapdance_storage));
    fault = 0;
    eeprom_update_byte((uint8_t *)350, 0x55);
    CHECK(era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 1, "NEW_SET_DEBT_SURVIVES_OLD_REPAIR");
    CHECK(eeprom_read_byte((uint8_t *)37) == 0x45, "OLD_INTENT_DURABLE");
    CHECK(era_tapdance_save_config(), "NEW_GENERATION_SAVE");
    CHECK(era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 0, "NEW_GENERATION_CONSUMED_ONCE");
    cold_byte(37, 0x46);
}

static void newer_failed_save(void) {
    fresh();
    semantic_td(0x45, 0x67);
    fault = 1;
    CHECK(!era_tapdance_save_config(), "FIRST_GENERATION_SAVE_FAILS");
    memset(tapdance_storage, 0x46, sizeof(tapdance_storage));
    era_state_sync_note_config_semantic_commit(0, sizeof(tapdance_storage));
    CHECK(!era_tapdance_save_config(), "NEW_GENERATION_SAVE_FAILS");
    fault = 0;
    eeprom_update_byte((uint8_t *)350, 0x55);
    CHECK(s_config_pending_persist_mask == 0 && era_state_sync_config_revision() == 4, "LATEST_ACCEPTED_GENERATION_REPAIRS");
    cold_byte(37, 0x46);
}

static void same_retry(void) {
    fresh();
    semantic_td(0x45, 0x67);
    semantic_tapping(0x89);
    fault = 1;
    CHECK(!era_tapdance_save_config(), "SAME_RETRY_FIRST_FAILURE");
    fault = 0;
    CHECK(era_tapdance_save_config(), "SAME_RETRY_SUCCESS");
    CHECK(era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 2, "SAME_RETRY_EXACT_OWNER");
    CHECK(era_tapping_save_config() && era_state_sync_config_revision() == 4, "DELAYED_SAVE_NO_EXTRA_REVISION");
    CHECK(era_tapdance_save_config() && era_state_sync_config_revision() == 4, "DURABLE_NOOP_NO_REVISION");
    cold_byte(209, 0x67);
}

static void unrelated_config(void) {
    fresh();
    semantic_td(0x45, 0x67);
    semantic_tapping(0x89);
    fault = 1;
    CHECK(!era_tapdance_save_config(), "UNRELATED_FIRST_FAILURE");
    fault = 0;
    eeprom_update_byte((uint8_t *)(ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET), 0x56);
    CHECK(era_state_sync_config_revision() == 5 && s_config_pending_persist_mask == 2, "UNRELATED_FIRMWARE_CHANGE_PUBLISHED");
    CHECK(era_tapping_save_config() && era_state_sync_config_revision() == 5, "UNRELATED_REPAIR_KEEPS_OTHER_OWNER");
}

static void compound_config(void) {
    for (unsigned failed = 0; failed < 2; ++failed) {
        fresh();
        memset(tapdance_storage, 0x45, sizeof(tapdance_storage));
        memset(tapdance_timing, 0x67, sizeof(tapdance_timing));
        fault = failed;
        bool ok = era_tapdance_save_config();
        CHECK(ok == !failed, "COMPOUND_RECEIPT");
        if (failed) {
            CHECK(era_state_sync_config_revision() == 1, "FAILED_COMPOUND_NOT_PUBLISHED");
            fault = 0;
            CHECK(era_tapdance_save_config(), "COMPOUND_RETRY");
        }
        CHECK(era_state_sync_config_revision() == 2, "COMPOUND_CONFIG_ONCE");
        CHECK(era_tapdance_save_config() && era_state_sync_config_revision() == 2, "COMPOUND_NOOP");
        cold_byte(209, 0x67);
    }
}

static void nested_domains(void) {
    fresh();
    uint32_t marker = ERA_STORAGE_DYNAMIC_MACRO_ADDR + dynamic_keymap_macro_get_buffer_size() - 1U;
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0xFF);
    CHECK(eeprom_begin_transaction() && eeprom_begin_transaction(), "NESTED_BEGIN");
    eeprom_update_byte((uint8_t *)350, 0x55);
    eeprom_update_byte((uint8_t *)380, 0x56);
    eeprom_update_byte((uint8_t *)(uintptr_t)ERA_STORAGE_DYNAMIC_MACRO_ADDR, 0x57);
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0);
    CHECK(eeprom_end_transaction(), "INNER_END");
    CHECK(era_state_sync_keymap_revision() == 1 && era_state_sync_macro_revision() == 1, "INNER_NOT_PUBLISHED");
    CHECK(eeprom_end_transaction(), "OUTER_END");
    CHECK(era_state_sync_keymap_revision() == 2 && era_state_sync_macro_revision() == 2, "DOMAIN_UNION_ONCE");
    cold_byte(marker, 0);
}

static void failed_nested_domains(void) {
    fresh();
    uint32_t marker = ERA_STORAGE_DYNAMIC_MACRO_ADDR + dynamic_keymap_macro_get_buffer_size() - 1U;
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0xFF);
    CHECK(eeprom_begin_transaction(), "FAILED_DOMAIN_BEGIN");
    eeprom_update_byte((uint8_t *)350, 0x55);
    fault = 1;
    eeprom_update_byte((uint8_t *)380, 0x56);
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0);
    CHECK(!eeprom_end_transaction(), "FAILED_DOMAIN_END");
    CHECK(era_state_sync_keymap_revision() == 1 && era_state_sync_macro_revision() == 1, "FAILED_DOMAIN_DEFERRED");
    fault = 0;
    eeprom_update_byte((uint8_t *)37, 0x54);
    CHECK(era_state_sync_keymap_revision() == 2 && era_state_sync_macro_revision() == 2 && era_state_sync_config_revision() == 2, "REPAIR_ALL_DOMAINS_ONCE");
    cold_byte(380, 0x56);
}

static void reopened_macro(void) {
    fresh();
    uint32_t marker = ERA_STORAGE_DYNAMIC_MACRO_ADDR + dynamic_keymap_macro_get_buffer_size() - 1U;
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0xFF);
    fault = 1;
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0);
    CHECK(!eeprom_persistence_ok() && era_state_sync_macro_revision() == 1, "FAILED_CLOSE_NOT_PUBLISHED");
    fault = 0;
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0xFF);
    CHECK(eeprom_persistence_ok() && era_state_sync_macro_revision() == 1, "REOPEN_CANCELS_OLD_CLOSE");
    eeprom_update_byte((uint8_t *)(uintptr_t)ERA_STORAGE_DYNAMIC_MACRO_ADDR, 0x57);
    eeprom_update_byte((uint8_t *)(uintptr_t)marker, 0);
    CHECK(era_state_sync_macro_revision() == 2, "FRESH_CLOSE_ONCE");
    cold_byte(marker, 0);
}

static void erased_intent(void) {
    fresh();
    semantic_td(0x45, 0x67);
    fault = 1;
    CHECK(!era_tapdance_save_config(), "ERASED_CONFIG_FAILS");
    eeprom_update_byte((uint8_t *)350, 0x55);
    semantic_tapping(0x89);
    fault = 0;
    unsigned resets = macro_session_resets;
    eeprom_driver_erase();
    CHECK(eeprom_persistence_ok() && macro_session_resets == resets + 1, "ERASE_DISCARDS_UPLOAD_PROOF");
    eeprom_update_byte((uint8_t *)0, 0x55); // No State Sync domain.
    CHECK(era_state_sync_keymap_revision() == 1 && era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 0x1003U, "ERASE_DISCARDS_INTENT_KEEPS_NEW_DEBT");
    CHECK(era_tapping_save_config() && era_state_sync_config_revision() == 4 && s_config_pending_persist_mask == 0x1001U, "POST_ERASE_OWNER_SAVE");
}

static void rejected_write(void) {
    fresh();
    eeprom_reject_write();
    CHECK(!eeprom_persistence_ok() && !wear_leveling_is_pending(), "PROTOCOL_REJECT_HAS_NO_IMAGE_INTENT");
    eeprom_update_byte((uint8_t *)37, 0x51);
    CHECK(eeprom_persistence_ok() && era_state_sync_keymap_revision() == 1 && era_state_sync_macro_revision() == 1, "REJECT_DID_NOT_CREATE_NOTIFICATION");
    fault = 1;
    eeprom_update_byte((uint8_t *)350, 0x55);
    CHECK(wear_leveling_is_pending(), "PHYSICAL_PENDING_EXISTS");
    eeprom_reject_write();
    CHECK(wear_leveling_is_pending(), "REJECT_KEEPS_PHYSICAL_PENDING");
    fault = 0;
    eeprom_update_byte((uint8_t *)38, 0x52);
    CHECK(eeprom_persistence_ok() && era_state_sync_keymap_revision() == 2, "PHYSICAL_PENDING_STILL_REPAIRS");
    CHECK(eeprom_begin_transaction(), "REJECT_SCOPE_BEGIN");
    eeprom_reject_write();
    CHECK(!eeprom_end_transaction(), "PROTOCOL_REJECT_SCOPE_STICKY");
}

static void noop_debt(void) {
    fresh();
    semantic_td(0x45, 0x67);
    semantic_td(0, 0); // Runtime edits returned to the already durable image.
    CHECK(era_tapdance_save_config(), "SEMANTIC_DURABLE_NOOP");
    CHECK(era_state_sync_config_revision() == 5 && s_config_pending_persist_mask == 0, "NOOP_CONSUMES_MATCHING_DEBT");
    fault = 1;
    eeprom_update_byte((uint8_t *)350, 0x55);
    fault = 0;
    CHECK(era_tapdance_save_config(), "NOOP_REPAIRS_PENDING_IMAGE");
    CHECK(era_state_sync_keymap_revision() == 2 && era_state_sync_config_revision() == 5, "NOOP_REPAIR_NOTIFY_ONCE");
    cold_byte(350, 0x55);
}

typedef struct { const char *name; void (*run)(void); } test_case_t;
static const test_case_t cases[] = {
    {"sparse_debt", sparse_debt}, {"newer_set", newer_set}, {"newer_failed_save", newer_failed_save},
    {"same_retry", same_retry}, {"unrelated_config", unrelated_config}, {"compound_config", compound_config},
    {"nested_domains", nested_domains}, {"failed_nested_domains", failed_nested_domains},
    {"reopened_macro", reopened_macro}, {"erased_intent", erased_intent}, {"rejected_write", rejected_write}, {"noop_debt", noop_debt},
};
int main(int argc, char **argv) {
    unsigned count = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], cases[i].name)) continue;
        cases[i].run();
        printf("PASS WL semantic intent %s\n", cases[i].name);
        ++count;
    }
    CHECK(count != 0, "KNOWN_TEST_CASE");
    return 0;
}
