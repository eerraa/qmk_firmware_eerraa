// SPDX-License-Identifier: GPL-2.0-or-later
// Production boot callers/initializers and storage adapters; physical media and
// optional board default payloads are modeled. Faults start after macro defaults,
// at the first final-valid magic append (WL) or outer commit program (NOR).
#include "eeconfig.h"
#include "via.h"
#if defined(EEPROM_CUSTOM)
#define main nor_tests_main
#include "nor.c"
#undef main
#else
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
#include "keyboards/era/common/storage/era_eeprom_config_io.h"
#include "keyboards/era/common/system/era_state_sync.h"
static uint16_t physical[WEAR_LEVELING_BACKING_SIZE / 2];
static unsigned programs, erases, fail_mode, fault;
bool backing_store_init(void) { return true; }
bool backing_store_unlock(void) { return true; }
bool backing_store_lock(void) { return true; }
bool backing_store_erase(void) { erases++; memset(physical, 0, sizeof(physical)); return true; }
bool backing_store_read(uint32_t a, backing_store_int_t *v) {
    assert(a % 2 == 0 && a + 2 <= sizeof(physical)); *v = physical[a / 2]; return true;
}
bool backing_store_write(uint32_t a, backing_store_int_t v) {
    programs++; assert(a % 2 == 0 && a + 2 <= sizeof(physical));
    if (fault == 1) return false;
    physical[a / 2] |= fault == 2 ? v & 0xFFU : v;
    return fault == 0 && physical[a / 2] == v;
}
void raw_hid_send(uint8_t *data, uint8_t length) { (void)data; (void)length; }
#define DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR 801U
#include "macro_functions.inc"
static void fresh(void) {
    memset(physical, 0, sizeof(physical)); programs = erases = fault = 0;
    eeprom_driver_init(); assert(eeprom_persistence_ok());
}
static void reboot(void) { fault = 0; eeprom_driver_init(); assert(eeprom_persistence_ok()); }
#endif
#include "boot_common.inc"

static unsigned fail_cut;
static bool arm_magic_failure;
static uint8_t preserved_prefix[VIA_EEPROM_MAGIC_ADDR];
static void fixture_after_macro_reset(void) {
    if (!arm_magic_failure) return;
#if defined(EEPROM_CUSTOM)
    fail_at = programs + fail_cut;
#else
    fault = fail_mode;
#endif
}
static void clear_fault(void) {
    arm_magic_failure = false;
#if defined(EEPROM_CUSTOM)
    fail_at = 0; fail_all = false; read_fail = false;
#else
    fault = 0;
#endif
}
static void preserve_config(void) {
    if (eeprom_read_byte((void *)2) != 0xA3) {
        fprintf(stderr, "preservation failure: mode=%u cut=%u QMK=%04x debug=%02x kb=%u ready=%u programs=%u erases=%u\n",
                fail_mode, fail_cut, eeprom_read_word((void *)0), eeprom_read_byte((void *)2),
                init_kb_calls, ready_calls, programs, erases);
    }
    assert(nvm_eeconfig_is_enabled() && !eeconfig_is_init_required());
    assert(eeprom_read_byte((void *)2) == 0xA3);
    assert(eeprom_read_byte((void *)3) == 4);
    assert(eeprom_read_word((void *)4) == 0x93A1);
    assert(eeprom_read_byte((void *)193) == 0x6B);
    uint8_t actual_prefix[sizeof(preserved_prefix)];
    eeprom_read_block(actual_prefix, (void *)0, sizeof(actual_prefix));
    assert(!memcmp(preserved_prefix, actual_prefix, sizeof(actual_prefix)));
    assert(!era_reset_guard_check());
    assert(init_kb_calls == 0);
}
static void seed(void) {
    clear_fault(); fresh(); eeconfig_init_failed = false;
#ifdef VIA_INIT_RECEIPT_FIXTURE
    via_init_failed = false;
#endif
    counts_clear(); boot(); full();
    eeprom_update_byte((void *)2, 0xA3);
    eeprom_update_byte((void *)3, 4);
    eeprom_update_word((void *)4, 0x93A1);
    eeprom_update_byte((void *)193, 0x6B);
    eeprom_update_byte((void *)256, 0x79);
    via_eeprom_set_valid(false);
    via_set_layout_options(3);
    eeprom_update_byte((void *)321, 0x86);
    assert(eeprom_persistence_ok());
    eeprom_read_block(preserved_prefix, (void *)0, sizeof(preserved_prefix));
    counts_clear(); preserve_config();
}
static void via_failure(void) {
    seed(); arm_magic_failure = true;
    via_init();
    assert(!eeprom_persistence_ok());
    preserve_config();
    assert(keymap_calls == 1 && macro_calls == 1);
#if !defined(EEPROM_CUSTOM)
    // Production logical reads expose desired valid magic although append failed.
    assert(via_eeprom_is_valid());
#endif
    assert(!eeconfig_is_enabled());
    if (!setjmp(stalled)) quantum_init();
    assert(ready_calls == 0 && !eeconfig_is_enabled());
    preserve_config();
    clear_fault(); counts_clear();
    // Retry through the production quantum caller, without a full-default owner.
    quantum_init();
    assert(ready_calls == 1 && eeconfig_is_enabled()); preserve_config();
    assert(keymap_calls == 1 && macro_calls == 1);
    reboot(); eeconfig_init_failed = false;
#ifdef VIA_INIT_RECEIPT_FIXTURE
    via_init_failed = false;
#endif
    counts_clear(); unsigned before = programs, before_erases = erases; boot();
    assert(ready_calls == 1 && keymap_calls == 0 && macro_calls == 0);
    assert(programs == before && erases == before_erases); preserve_config();
}
static void cold_failure(void) {
    seed(); arm_magic_failure = true; via_init();
    assert(!eeprom_persistence_ok()); clear_fault(); reboot();
    eeconfig_init_failed = false;
#ifdef VIA_INIT_RECEIPT_FIXTURE
    via_init_failed = false;
#endif
    counts_clear(); boot(); assert(ready_calls == 1 && eeconfig_is_enabled()); preserve_config();
}
static void foreign_scope(void) {
    seed(); via_init(); assert(eeconfig_is_enabled()); counts_clear();
    assert(eeprom_begin_transaction()); unsigned before = programs;
    eeconfig_init_via();
    assert(eeprom_transaction_active() && !eeconfig_is_enabled());
    assert(programs == before && keymap_calls == 0 && macro_calls == 0);
    assert(eeprom_end_transaction()); assert(!eeconfig_is_enabled());
    preserve_config(); quantum_init();
    assert(ready_calls == 1 && eeconfig_is_enabled()); preserve_config();
    assert(keymap_calls == 1 && macro_calls == 1);
    puts("PASS standalone VIA refuses foreign outer scope before mutation and retains completion gate");
}
#if defined(EEPROM_CUSTOM)
static void full_outer_failure(void) {
    // A healthy staged VIA payload inside full defaults may not acknowledge its
    // enclosing NOR factory commit. The previous durable store has valid magic.
    seed(); via_init(); unsigned before = programs; eeconfig_init();
    unsigned full_programs = programs - before; assert(full_programs >= 2);
    const unsigned cuts[] = {1, full_programs};
    for (volatile unsigned cut = 0; cut < sizeof(cuts) / sizeof(cuts[0]); cut++) {
        for (fail_mode = 0; fail_mode < 4; fail_mode++) {
            seed(); via_init(); counts_clear(); eeconfig_require_reset();
            fail_cut = cuts[cut]; arm_magic_failure = true; eeconfig_init();
            assert(init_kb_calls == 1 && keymap_calls == 1 && macro_calls == 1);
            assert(via_eeprom_is_valid() && nvm_eeconfig_is_enabled());
            assert(eeconfig_is_init_required() && !eeconfig_is_enabled());
            if (!setjmp(stalled)) quantum_init();
            assert(ready_calls == 0 && eeconfig_is_init_required());
            clear_fault(); counts_clear(); quantum_init();
            assert(ready_calls == 1); full();
        }
    }
    puts("PASS nested VIA/default owners run once while full outer failure retains the reset-required boot gate");
}
#endif
int main(void) {
#if defined(EEPROM_CUSTOM)
    // Force a changed outer envelope even though invalid VIA magic is restored
    // before committing; cover record payload and final seal/readback separately.
    seed(); unsigned before = programs; via_init();
    unsigned outer_programs = programs - before;
    assert(outer_programs >= 2);
    for (fail_cut = 1; fail_cut <= outer_programs; fail_cut++) {
        for (fail_mode = 0; fail_mode < 4; fail_mode++) {
            via_failure(); cold_failure();
        }
    }
#else
    fail_cut = 1;
    for (fail_mode = 1; fail_mode <= 3; fail_mode++) {
        via_failure();
        // An untouched first append can cold-replay the old valid QMK store.
        // Torn ordinary WL log headers have a separate, existing cold-replay
        // limitation; this fixture never promises their old-or-new rollback.
        if (fail_mode == 1) cold_failure();
    }
#endif
    foreign_scope();
#if defined(EEPROM_CUSTOM)
    full_outer_failure();
#endif
    puts("PASS VIA-only failure blocks boot, retries only VIA, preserves the full QMK/ERA prefix through successful retry/remount and valid no-write boot");
    return 0;
}
