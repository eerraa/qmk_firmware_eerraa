// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_state_sync.h"

#include "via.h"
#include "raw_hid.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "nvm_eeprom_via_internal.h"
#include "nvm_dynamic_keymap.h"
#include "keycode_config.h"
#include "../storage/era_eeprom_layout.h"

#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix_types.h"
#endif

#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
#    include "../split/era_split_eeprom_sync.h"
#endif

#ifdef ERA_VIA_SYSTEM_ENABLE
#    include "era_via_system.h"
#endif

static uint32_t s_keymap_revision = 1;
static uint32_t s_macro_revision  = 1;
static uint32_t s_config_revision = 1;
/* A semantic setter publishes runtime before VIA's later SAVE. Track that debt
   per EEPROM region so only the matching synchronous persist is suppressed;
   unrelated firmware-side writes must still invalidate CONFIG. */
static uint16_t s_config_pending_persist_mask;
static uint16_t s_config_active_persist_mask;

#define ERA_STATE_SYNC_CONFIG_REGION_TAP_DANCE (1U << 0)
#define ERA_STATE_SYNC_CONFIG_REGION_TAPPING (1U << 1)
#define ERA_STATE_SYNC_CONFIG_REGION_MOUSEKEY (1U << 2)
#define ERA_STATE_SYNC_CONFIG_REGION_DEBOUNCE (1U << 3)
#define ERA_STATE_SYNC_CONFIG_REGION_SOCD_LR (1U << 4)
#define ERA_STATE_SYNC_CONFIG_REGION_SOCD_UD (1U << 5)
#define ERA_STATE_SYNC_CONFIG_REGION_KKUK (1U << 6)
#define ERA_STATE_SYNC_CONFIG_REGION_BACKLIGHT (1U << 7)
#define ERA_STATE_SYNC_CONFIG_REGION_RGB_INDICATOR (1U << 8)
#define ERA_STATE_SYNC_CONFIG_REGION_KEYBOARD (1U << 9)
#define ERA_STATE_SYNC_CONFIG_REGION_RESERVED (1U << 10)

static uint32_t era_state_sync_next(uint32_t value) {
    value++;
    if (value == 0) {
        value = 1;
    }
    return value;
}

static void era_state_sync_bump_keymap(void) {
    s_keymap_revision = era_state_sync_next(s_keymap_revision);
}

static void era_state_sync_bump_macro(void) {
    s_macro_revision = era_state_sync_next(s_macro_revision);
}

static void era_state_sync_bump_config(void) {
    s_config_revision = era_state_sync_next(s_config_revision);
}

#ifdef ERA_STATE_SYNC_TEST
uint32_t era_state_sync_keymap_revision(void) {
    return s_keymap_revision;
}

uint32_t era_state_sync_macro_revision(void) {
    return s_macro_revision;
}

uint32_t era_state_sync_config_revision(void) {
    return s_config_revision;
}
#endif

static bool era_state_sync_span_overlaps(uint16_t offset, uint16_t length, uint32_t start, uint32_t size) {
    if (length == 0 || size == 0) {
        return false;
    }
    uint32_t end       = (uint32_t)offset + length;
    uint32_t range_end = (uint32_t)start + size;
    return offset < range_end && start < end;
}

static uint16_t era_state_sync_config_region_mask(uint16_t offset, uint16_t length) {
    uint16_t mask = 0;
#define ERA_STATE_SYNC_ADD_CONFIG_REGION(bit, region_offset, region_size)                                                     \
    do {                                                                                                                     \
        if (era_state_sync_span_overlaps(offset, length, ERA_EEPROM_CONFIG_ADDR + (region_offset), (region_size))) {         \
            mask |= (bit);                                                                                                  \
        }                                                                                                                    \
    } while (0)
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_TAP_DANCE, ERA_EEPROM_TAP_DANCE_CONFIG_OFFSET, ERA_EEPROM_TAP_DANCE_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_TAPPING, ERA_EEPROM_TAPPING_CONFIG_OFFSET, ERA_EEPROM_TAPPING_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_MOUSEKEY, ERA_EEPROM_MOUSEKEY_CONFIG_OFFSET, ERA_EEPROM_MOUSEKEY_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_DEBOUNCE, ERA_EEPROM_DEBOUNCE_CONFIG_OFFSET, ERA_EEPROM_DEBOUNCE_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_SOCD_LR, ERA_EEPROM_SOCD_LR_CONFIG_OFFSET, ERA_EEPROM_SOCD_LR_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_SOCD_UD, ERA_EEPROM_SOCD_UD_CONFIG_OFFSET, ERA_EEPROM_SOCD_UD_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_KKUK, ERA_EEPROM_KKUK_CONFIG_OFFSET, ERA_EEPROM_KKUK_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_BACKLIGHT, ERA_EEPROM_BACKLIGHT_CONFIG_OFFSET, ERA_EEPROM_BACKLIGHT_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_RGB_INDICATOR, ERA_EEPROM_RGB_INDICATOR_CONFIG_OFFSET, ERA_EEPROM_RGB_INDICATOR_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_KEYBOARD, ERA_EEPROM_KEYBOARD_CONFIG_OFFSET, ERA_EEPROM_KEYBOARD_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_RESERVED, ERA_EEPROM_SYNCABLE_RESERVED_OFFSET, ERA_EEPROM_SYNCABLE_RESERVED_SIZE);
#undef ERA_STATE_SYNC_ADD_CONFIG_REGION
    return mask;
}

void era_state_sync_note_config_semantic_commit(uint16_t config_offset, uint16_t length) {
    s_config_pending_persist_mask |= era_state_sync_config_region_mask(ERA_EEPROM_CONFIG_ADDR + config_offset, length);
    era_state_sync_bump_config();
}

void era_state_sync_config_persist_begin(uint16_t config_offset, uint16_t length) {
    uint16_t mask                 = era_state_sync_config_region_mask(ERA_EEPROM_CONFIG_ADDR + config_offset, length);
    s_config_active_persist_mask = s_config_pending_persist_mask & mask;
    s_config_pending_persist_mask &= (uint16_t)~mask;
}

void era_state_sync_config_persist_end(void) {
    s_config_active_persist_mask = 0;
}

void era_state_sync_note_eeprom_span(uint16_t offset, uint16_t length) {
    uint32_t keymap_start = nvm_dynamic_keymap_eeprom_address();
    uint32_t macro_start  = nvm_dynamic_keymap_macro_eeprom_address();
    if (macro_start > keymap_start && era_state_sync_span_overlaps(offset, length, keymap_start, macro_start - keymap_start)) {
        era_state_sync_bump_keymap();
    }
    if (era_state_sync_span_overlaps(offset, length, macro_start, nvm_dynamic_keymap_macro_size()) && !nvm_dynamic_keymap_macro_transaction_in_progress()) {
        era_state_sync_bump_macro();
    }
    if (era_state_sync_span_overlaps(offset, length, ERA_EEPROM_CONFIG_ADDR + ERA_EEPROM_SYNCABLE_CONFIG_OFFSET, ERA_EEPROM_SYNCABLE_CONFIG_SIZE)) {
        uint16_t changed_region_mask = era_state_sync_config_region_mask(offset, length);
        if ((changed_region_mask & (uint16_t)~s_config_active_persist_mask) != 0) {
            era_state_sync_bump_config();
        }
        return;
    }
#ifdef RGB_MATRIX_ENABLE
    if (era_state_sync_span_overlaps(offset, length, (uint16_t)(uintptr_t)EECONFIG_RGB_MATRIX, sizeof(rgb_config_t))) {
        era_state_sync_bump_config();
        return;
    }
#endif
    if (era_state_sync_span_overlaps(offset, length, (uint16_t)(uintptr_t)EECONFIG_KEYMAP, sizeof(keymap_config_t))) {
        era_state_sync_bump_config();
        return;
    }
    if (era_state_sync_span_overlaps(offset, length, (uint16_t)(uintptr_t)EECONFIG_DEFAULT_LAYER, sizeof(uint8_t))) {
        era_state_sync_bump_config();
        return;
    }
#ifdef VIA_EEPROM_LAYOUT_OPTIONS_ADDR
    if (era_state_sync_span_overlaps(offset, length, VIA_EEPROM_LAYOUT_OPTIONS_ADDR, VIA_EEPROM_LAYOUT_OPTIONS_SIZE)) {
        era_state_sync_bump_config();
    }
#endif
}

void era_state_sync_note_storage_domain(uint8_t domain) {
#ifdef ERA_HOST_PEER_STORAGE_V1_ENABLE
    switch (domain) {
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_KEYMAP:
            era_state_sync_bump_keymap();
            break;
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_DYNAMIC_MACRO:
            era_state_sync_bump_macro();
            break;
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_ERA_CONFIG:
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_RGB_MATRIX:
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_KEYMAP_CONFIG:
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_QMK_DEFAULT_LAYER:
        case ERA_SPLIT_EEPROM_SYNC_DOMAIN_VIA_LAYOUT_OPTIONS:
            era_state_sync_bump_config();
            break;
        default:
            break;
    }
#else
    (void)domain;
#endif
}

static void era_state_sync_put_be32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

bool era_state_sync_via_command(uint8_t *data, uint8_t length) {
    if (!data || length < 2 || data[0] != id_get_keyboard_value || data[1] != ERA_STATE_SYNC_KEYBOARD_VALUE) {
        return false;
    }
    if (length < 32) {
        return false;
    }

    uint8_t version = data[2];
    uint8_t tag_hi  = data[4];
    uint8_t tag_lo  = data[5];
    bool    valid   = data[3] == 0;
    uint8_t i;
    for (i = 6; i < 32; i++) {
        valid = valid && data[i] == 0;
    }
    for (i = 2; i < 32; i++) {
        data[i] = 0;
    }
    data[0] = id_get_keyboard_value;
    data[1] = ERA_STATE_SYNC_KEYBOARD_VALUE;
    data[2] = ERA_STATE_SYNC_ENVELOPE_VERSION;
    data[4] = tag_hi;
    data[5] = tag_lo;
    if (version != ERA_STATE_SYNC_ENVELOPE_VERSION) {
        data[3] = ERA_STATE_SYNC_STATUS_UNSUPPORTED_VERSION;
        raw_hid_send(data, 32);
        return true;
    }
    if (!valid) {
        data[3] = ERA_STATE_SYNC_STATUS_INVALID;
        raw_hid_send(data, 32);
        return true;
    }
    data[3] = ERA_STATE_SYNC_STATUS_OK;
    data[6] = ERA_STATE_SYNC_DOMAIN_MASK_INITIAL;
    era_state_sync_put_be32(&data[8], s_keymap_revision);
    era_state_sync_put_be32(&data[12], s_macro_revision);
    era_state_sync_put_be32(&data[16], s_config_revision);
    raw_hid_send(data, 32);
    return true;
}

#ifdef ERA_STATE_SYNC_TEST
void era_state_sync_set_revisions_for_testing(uint32_t keymap, uint32_t macro, uint32_t config) {
    s_keymap_revision                = keymap;
    s_macro_revision                 = macro;
    s_config_revision                = config;
    s_config_pending_persist_mask    = 0;
    s_config_active_persist_mask     = 0;
}
#endif

#ifndef ERA_VIA_SYSTEM_ENABLE
bool via_command_kb(uint8_t *data, uint8_t length) {
    return era_state_sync_via_command(data, length);
}
#endif

#ifndef ERA_HOST_PEER_STORAGE_V1_ENABLE
void nvm_eeprom_changed_kb(uint16_t offset, uint16_t length) {
    if (length == 0) {
        return;
    }
    era_state_sync_note_eeprom_span(offset, length);
}
#endif
