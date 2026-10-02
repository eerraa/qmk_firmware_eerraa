// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "keyboards/era/common/storage/era_eeprom_layout.h"
#include "keyboards/era/common/split/era_split_sync_policy.h"
#define ATOMIC_BLOCK_RESTORESTATE for (int once = 1; once; once = 0)
#define ERA_SPLIT_SCHEDULER_DIRTY_SYNC_POLICY 1
static uint8_t eeprom[280];
static int feature_init_calls, config_loads, guard_checks, guard_writes, user_inits;
static int stored_feature, loaded_feature;
static unsigned era_eeprom_read_config(void *b, unsigned a, unsigned n) { memcpy(b, eeprom + a, n); return n; }
static void era_eeprom_update_config(const void *b, unsigned a, unsigned n) { memcpy(eeprom + a, b, n); }
static void era_split_transport_scheduler_mark_dirty(unsigned flags) { (void)flags; }
/* SYNC_POLICY */
typedef uint8_t led_t;
static led_t tomak_host_led_state, tomak_usb_led_state;
static int g_tomak_config;
static void era_reset_guard_check(void) { guard_checks++; }
static void era_reset_guard_write(void) { guard_writes++; }
static void tomak_read_config_from_era_eeprom(int *c) { *c = stored_feature; config_loads++; }
static void era_board_config_load(void) { config_loads++; }
static uint8_t host_keyboard_led_state(void) { return 0; }
static uint8_t usb_device_state_get_leds(void) { return 0; }
static void matrix_init_user(void) { user_inits++; assert(feature_init_calls == 1); assert(loaded_feature == stored_feature); }
static void eeconfig_init_user(void) { assert(loaded_feature == stored_feature); }
static void era_common_features_init(void) { feature_init_calls++; loaded_feature = stored_feature; }
// Matrix-engine initialization resets debounce runtime to defaults. Loading
// saved features must follow it, before the first scan/user init in all keymaps.
static void era_matrix_engine_init(void) { loaded_feature = 200; }
// The physical erase and board defaults are boundary stubs, not NVM tests.
static void era_board_config_reset(void) { stored_feature = 200; }
static void tomak_era_eeprom_strict_reset(void) { memset(eeprom, 0xff, sizeof eeprom); stored_feature = 200; }
/* BOARD_HOOKS */

int main(void) {
    era_split_sync_policy_storage_t saved = {
        .signature = ERA_SPLIT_SYNC_POLICY_STORAGE_SIGNATURE,
        .flags_generation = 7, .eeprom_policy_generation = 3,
        .version = ERA_SPLIT_SYNC_POLICY_STORAGE_VERSION, .flags = 1,
    };
    memcpy(eeprom + ERA_SPLIT_EEPROM_SYNC_POLICY_CONFIG_OFFSET, &saved, sizeof saved);
    stored_feature = 137;
#ifdef VIA_ENABLE
    via_init_kb();
    assert(guard_checks == 1 && feature_init_calls == 0 && config_loads == 0);
#endif
    matrix_init();
    assert(feature_init_calls == 1);
    assert(config_loads == 1 && user_inits == 1 && loaded_feature == 137);
#ifndef NONSPLIT
    bool input = true, rgb = true;
    era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input);
    era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb);
    assert(!input && !rgb);
#endif
    // quantum_init's invalid-eeconfig path comes after matrix init. It must
    // replace the loaded runtime settings with the newly written defaults.
    eeconfig_init_kb();
    assert(feature_init_calls == 2 && loaded_feature == 200 && guard_writes == 1);
#ifndef NONSPLIT
    era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_INPUT, &input);
    era_split_sync_policy_get_requested(ERA_SPLIT_SYNC_POLICY_FIELD_RGB, &rgb);
    assert(input && rgb);
#endif
}
