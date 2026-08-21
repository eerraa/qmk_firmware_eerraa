// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_tapping.h"

#include <string.h>
#include "../storage/era_eeprom_storage.h"

#define ERA_TAPPING_SIGNATURE       0x50415447UL
#define ERA_TAPPING_VERSION         1U
#define ERA_TAPPING_TERM_MIN_MS     100U
#define ERA_TAPPING_TERM_MAX_MS     500U
#define ERA_TAPPING_TERM_STEP_MS    20U
#define ERA_TAPPING_TERM_DEFAULT_MS 200U

typedef struct __attribute__((packed)) {
    uint16_t tapping_term_ms;
    uint8_t  permissive_hold;
    uint8_t  hold_on_other_key_press;
    uint8_t  retro_tapping;
    uint8_t  version;
    uint8_t  reserved[2];
    uint32_t signature;
} era_tapping_config_t;

typedef struct {
    uint16_t tapping_term_ms;
    bool     permissive_hold;
    bool     hold_on_other_key_press;
    bool     retro_tapping;
} era_tapping_state_t;

_Static_assert(sizeof(era_tapping_config_t) == ERA_EEPROM_TAPPING_CONFIG_SIZE, "ERA tapping config size changed.");

static era_tapping_config_t tapping_config;
static era_tapping_state_t  tapping_state = {
    .tapping_term_ms         = ERA_TAPPING_TERM_DEFAULT_MS,
    .permissive_hold         = false,
    .hold_on_other_key_press = false,
    .retro_tapping           = false,
};

static uint16_t era_tapping_normalize_term(uint16_t term_ms) {
    term_ms = MIN(MAX(term_ms, ERA_TAPPING_TERM_MIN_MS), ERA_TAPPING_TERM_MAX_MS);
    return ERA_TAPPING_TERM_MIN_MS + (((term_ms - ERA_TAPPING_TERM_MIN_MS) / ERA_TAPPING_TERM_STEP_MS) * ERA_TAPPING_TERM_STEP_MS);
}

static bool era_tapping_config_is_valid(const era_tapping_config_t *config) {
    if (!config || config->signature != ERA_TAPPING_SIGNATURE || config->version != ERA_TAPPING_VERSION) {
        return false;
    }
    if (config->tapping_term_ms < ERA_TAPPING_TERM_MIN_MS || config->tapping_term_ms > ERA_TAPPING_TERM_MAX_MS) {
        return false;
    }
    return config->permissive_hold <= 1 && config->hold_on_other_key_press <= 1 && config->retro_tapping <= 1;
}

static void era_tapping_apply_defaults(void) {
    memset(&tapping_config, 0, sizeof(tapping_config));
    tapping_config.tapping_term_ms = ERA_TAPPING_TERM_DEFAULT_MS;
    tapping_config.version         = ERA_TAPPING_VERSION;
    tapping_config.signature       = ERA_TAPPING_SIGNATURE;
}

static void era_tapping_sync_state_from_config(void) {
    tapping_state.tapping_term_ms         = era_tapping_normalize_term(tapping_config.tapping_term_ms);
    tapping_state.permissive_hold         = tapping_config.permissive_hold != 0;
    tapping_state.hold_on_other_key_press = tapping_config.hold_on_other_key_press != 0;
    tapping_state.retro_tapping           = tapping_config.retro_tapping != 0;

    tapping_config.tapping_term_ms         = tapping_state.tapping_term_ms;
    tapping_config.permissive_hold         = tapping_state.permissive_hold ? 1 : 0;
    tapping_config.hold_on_other_key_press = tapping_state.hold_on_other_key_press ? 1 : 0;
    tapping_config.retro_tapping           = tapping_state.retro_tapping ? 1 : 0;
    memset(tapping_config.reserved, 0, sizeof(tapping_config.reserved));
}

void era_tapping_save_config(void) {
    era_eeprom_update_config(&tapping_config, ERA_EEPROM_TAPPING_CONFIG_OFFSET, sizeof(tapping_config));
}

static bool era_tapping_load_from_eeprom(bool write_defaults) {
    bool dirty = false;

    if (era_eeprom_read_config(&tapping_config, ERA_EEPROM_TAPPING_CONFIG_OFFSET, sizeof(tapping_config)) != sizeof(tapping_config) || !era_tapping_config_is_valid(&tapping_config)) {
        era_tapping_apply_defaults();
        dirty = true;
    } else {
        era_tapping_config_t previous = tapping_config;
        era_tapping_sync_state_from_config();
        dirty = memcmp(&previous, &tapping_config, sizeof(tapping_config)) != 0;
    }

    era_tapping_sync_state_from_config();

    if (write_defaults && dirty) {
        era_tapping_save_config();
    }
    return true;
}

void era_tapping_init(void) {
    era_tapping_load_from_eeprom(true);
}

void era_tapping_reload_from_eeprom(void) {
    era_tapping_load_from_eeprom(false);
}

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return tapping_state.tapping_term_ms;
}

bool get_permissive_hold(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return tapping_state.permissive_hold;
}

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return tapping_state.hold_on_other_key_press;
}

bool get_retro_tapping(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    (void)record;
    return tapping_state.retro_tapping;
}

#ifdef SPECULATIVE_HOLD
// FA-2 S1: speculation arms from hold-on-other-key-press alone (owner decision
// 2026-08-05) — it is that option extended to keys the engine cannot see, so
// the semantics a user already chose are the semantics they get across the
// seam. No independent kill-switch, no policy state: fresh defaults leave this
// false and the whole speculative family dark. The bridge composes with the
// family's revert-invisibility rule rather than replacing it (owner decision
// 2026-08-05, on the pre-device review's finding): a layer revert is
// firmware-local and a bare Ctrl/Shift down-up is OS-inert, but a bare
// GUI/Alt down-up is an OS action — the Start-menu toggle upstream's weak
// default exists to refuse — so the mod-tap half keeps upstream's own
// composition test. Per-key opt-in stays a keymap-level override.
bool get_speculative_hold(uint16_t keycode, keyrecord_t *record) {
    (void)record;
    if (!tapping_state.hold_on_other_key_press) {
        return false;
    }
    if (IS_QK_MOD_TAP(keycode)) {
        const uint8_t mods = mod_config(QK_MOD_TAP_GET_MODS(keycode));
        return (mods & (MOD_LCTL | MOD_LSFT)) == (mods & (MOD_HYPR));
    }
    return true; // The ERA layer-tap half: a layer revert is invisible outside the firmware.
}
#endif

void era_tapping_set_term_ms(uint16_t term_ms) {
    tapping_config.tapping_term_ms = era_tapping_normalize_term(term_ms);
    era_tapping_sync_state_from_config();
}

void era_tapping_set_permissive_hold(bool enabled) {
    tapping_config.permissive_hold = enabled ? 1 : 0;
    era_tapping_sync_state_from_config();
}

void era_tapping_set_hold_on_other_key_press(bool enabled) {
    tapping_config.hold_on_other_key_press = enabled ? 1 : 0;
    era_tapping_sync_state_from_config();
}

void era_tapping_set_retro_tapping(bool enabled) {
    tapping_config.retro_tapping = enabled ? 1 : 0;
    era_tapping_sync_state_from_config();
}

uint16_t era_tapping_get_term_ms(void) {
    return tapping_state.tapping_term_ms;
}

bool era_tapping_get_permissive_hold(void) {
    return tapping_state.permissive_hold;
}

bool era_tapping_get_hold_on_other_key_press(void) {
    return tapping_state.hold_on_other_key_press;
}

bool era_tapping_get_retro_tapping(void) {
    return tapping_state.retro_tapping;
}
