// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_state_sync.h"

#include "via.h"
#include "raw_hid.h"
#include "keycode_config.h"
#include "dynamic_keymap.h"
#include "eeprom.h"
#include "../storage/era_eeprom_layout.h"
#include "../storage/era_storage_layout.h"

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
static uint16_t s_config_active_debt_mask;
static uint16_t s_config_current_persist_mask;
static uint16_t s_config_current_debt_mask;

#ifdef EEPROM_WEAR_LEVELING
/* Already classified accepted writes, never the gaps of a physical envelope.
 * Publication and matching SET debt are separate: a later SET invalidates the
 * old intent's right to consume its debt without republishing that old SET. */
static uint32_t s_wl_pending_change_mask;
static uint32_t s_wl_completed_change_mask;
static uint16_t s_wl_pending_debt_mask;
#endif

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
#define ERA_STATE_SYNC_CONFIG_REGION_LIGHTING_SLEEP (1U << 11)
#define ERA_STATE_SYNC_CONFIG_REGION_TAP_DANCE_TIMING (1U << 12)

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

void era_state_sync_note_macro_public_change(void) { era_state_sync_bump_macro(); }

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
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_LIGHTING_SLEEP, ERA_EEPROM_LIGHTING_SLEEP_CONFIG_OFFSET, ERA_EEPROM_LIGHTING_SLEEP_CONFIG_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_TAP_DANCE_TIMING, ERA_EEPROM_TAP_DANCE_TIMING_OFFSET, ERA_EEPROM_TAP_DANCE_TIMING_SIZE);
    ERA_STATE_SYNC_ADD_CONFIG_REGION(ERA_STATE_SYNC_CONFIG_REGION_RESERVED, ERA_EEPROM_SYNCABLE_RESERVED_OFFSET, ERA_EEPROM_SYNCABLE_RESERVED_SIZE);
#undef ERA_STATE_SYNC_ADD_CONFIG_REGION
    return mask;
}

void era_state_sync_note_config_semantic_commit(uint16_t config_offset, uint16_t length) {
    uint16_t mask = era_state_sync_config_region_mask(ERA_EEPROM_CONFIG_ADDR + config_offset, length);
    s_config_pending_persist_mask |= mask;
    s_config_active_debt_mask &= (uint16_t)~mask;
    s_config_current_debt_mask &= (uint16_t)~mask;
#ifdef EEPROM_WEAR_LEVELING
    s_wl_pending_debt_mask &= (uint16_t)~mask;
#endif
    era_state_sync_bump_config();
}

void era_state_sync_config_persist_begin(uint16_t config_offset, uint16_t length) {
    uint16_t mask                 = era_state_sync_config_region_mask(ERA_EEPROM_CONFIG_ADDR + config_offset, length);
    s_config_current_persist_mask = s_config_pending_persist_mask & mask;
    s_config_current_debt_mask    = s_config_current_persist_mask;
    s_config_active_persist_mask |= s_config_current_persist_mask;
    s_config_active_debt_mask    |= s_config_current_debt_mask;
}

void eeprom_transaction_complete(bool success) {
    if (success) s_config_pending_persist_mask &= (uint16_t)~s_config_active_debt_mask;
#ifdef EEPROM_WEAR_LEVELING
    uint32_t completed = s_wl_completed_change_mask;
    if (!success) {
        s_wl_pending_change_mask |= completed;
        s_wl_pending_debt_mask |= s_config_active_debt_mask;
    }
    s_wl_completed_change_mask = 0;
#endif
    s_config_active_persist_mask = 0;
    s_config_active_debt_mask = 0;
#ifdef EEPROM_WEAR_LEVELING
    if (success) era_state_sync_note_eeprom_mask(completed);
#endif
}

void era_state_sync_config_persist_end(void) {
    s_config_current_persist_mask = 0;
    s_config_current_debt_mask = 0;
    if (!eeprom_transaction_active()) eeprom_transaction_complete(eeprom_persistence_ok());
}


/* The journal may cover gaps between edits. Classify the changed spans before
 * staging and union their meanings; one durable operation publishes each
 * revision at most once. These masks are RAM-only and never go on the wire. */
#define ERA_STATE_SYNC_CHANGE_KEYMAP (1UL << 16)
#define ERA_STATE_SYNC_CHANGE_MACRO (1UL << 17)
#define ERA_STATE_SYNC_CHANGE_CORE_CONFIG (1UL << 18)

uint32_t era_state_sync_eeprom_span_mask(uint16_t offset, uint16_t length) {
    uint32_t mask = era_state_sync_config_region_mask(offset, length);
    if (era_state_sync_span_overlaps(offset, length, ERA_STORAGE_DYNAMIC_KEYMAP_ADDR, ERA_STORAGE_DYNAMIC_MACRO_ADDR - ERA_STORAGE_DYNAMIC_KEYMAP_ADDR)) mask |= ERA_STATE_SYNC_CHANGE_KEYMAP;
    if (era_state_sync_span_overlaps(offset, length, ERA_STORAGE_DYNAMIC_MACRO_ADDR, dynamic_keymap_macro_get_buffer_size())) mask |= ERA_STATE_SYNC_CHANGE_MACRO;
#ifdef RGB_MATRIX_ENABLE
    if (era_state_sync_span_overlaps(offset, length, ERA_STORAGE_EECONFIG_RGB_MATRIX_ADDR, sizeof(rgb_config_t))) mask |= ERA_STATE_SYNC_CHANGE_CORE_CONFIG;
#endif
    if (era_state_sync_span_overlaps(offset, length, ERA_STORAGE_EECONFIG_KEYMAP_ADDR, sizeof(keymap_config_t)) ||
        era_state_sync_span_overlaps(offset, length, ERA_STORAGE_EECONFIG_DEFAULT_LAYER_ADDR, sizeof(uint8_t)) ||
        era_state_sync_span_overlaps(offset, length, ERA_STORAGE_VIA_LAYOUT_OPTIONS_ADDR, VIA_EEPROM_LAYOUT_OPTIONS_SIZE)) mask |= ERA_STATE_SYNC_CHANGE_CORE_CONFIG;
    return mask;
}

void era_state_sync_note_eeprom_mask(uint32_t mask) {
    if (mask & ERA_STATE_SYNC_CHANGE_KEYMAP) era_state_sync_bump_keymap();
    if (mask & ERA_STATE_SYNC_CHANGE_MACRO) era_state_sync_bump_macro();
    if ((mask & ERA_STATE_SYNC_CHANGE_CORE_CONFIG) || ((uint16_t)mask & (uint16_t)~s_config_active_persist_mask)) era_state_sync_bump_config();
}

void era_state_sync_note_eeprom_span(uint16_t offset, uint16_t length) {
    era_state_sync_note_eeprom_mask(era_state_sync_eeprom_span_mask(offset, length));
}

#ifdef EEPROM_WEAR_LEVELING
static uint8_t s_macro_marker_before;
static uint8_t era_state_sync_public_macro_marker(void) {
    uint32_t marker = ERA_STORAGE_DYNAMIC_MACRO_ADDR + dynamic_keymap_macro_get_buffer_size() - 1U;
    return eeprom_persistence_ok() ? eeprom_read_byte((const uint8_t *)(uintptr_t)marker) : 0xFFU;
}

void eeprom_driver_note_write_start(void) {
    s_macro_marker_before = era_state_sync_public_macro_marker();
}

static void era_state_sync_note_public_write(uint32_t address, size_t length, bool changed) {
    bool payload = changed && era_state_sync_span_overlaps(address, length, ERA_STORAGE_DYNAMIC_MACRO_ADDR, dynamic_keymap_macro_get_buffer_size() - 1U);
    if (payload || s_macro_marker_before != era_state_sync_public_macro_marker()) era_state_sync_bump_macro();
}
#endif

#ifdef EEPROM_WEAR_LEVELING
static uint32_t era_state_sync_wl_span_mask(uint32_t address, size_t length) {
    if (length == 0U) return 0;
    uint32_t mask = era_state_sync_eeprom_span_mask((uint16_t)address, (uint16_t)length);
    /* MACRO is published at public visibility, independently of receipts. */
    mask &= ~ERA_STATE_SYNC_CHANGE_MACRO;
    return mask;
}

void eeprom_driver_reset_write_observer(void) {
    s_wl_pending_change_mask = 0;
    s_wl_completed_change_mask = 0;
    s_wl_pending_debt_mask = 0;
    s_config_active_persist_mask = 0;
    s_config_active_debt_mask = 0;
    s_config_current_persist_mask = 0;
    s_config_current_debt_mask = 0;
}

void eeprom_driver_note_write_result(uint32_t address, size_t length, bool changed, bool success) {
    era_state_sync_note_public_write(address, length, changed);
    if (length == 0U) return;
    uint32_t span = era_state_sync_wl_span_mask(address, length);
    uint16_t config = (uint16_t)span;
    uint16_t debt = config & s_config_current_debt_mask;
    /* A new accepted write can supersede the old intent in the same region.
     * Only the SET snapshot captured for this write can consume current debt. */
    s_wl_pending_debt_mask = (s_wl_pending_debt_mask & (uint16_t)~config) | debt;
    if (eeprom_transaction_active()) {
        s_config_active_debt_mask = (s_config_active_debt_mask & (uint16_t)~config) | debt;
    }
    uint32_t mask = changed ? span & ~(uint32_t)s_config_current_persist_mask : 0;
    if (!success) {
        s_wl_pending_change_mask |= mask;
        return;
    }
    mask |= s_wl_pending_change_mask;
    debt = s_wl_pending_debt_mask;
    s_wl_pending_change_mask = 0;
    s_wl_pending_debt_mask = 0;
    if (eeprom_transaction_active()) {
        s_wl_completed_change_mask |= mask;
        s_config_active_debt_mask |= debt;
    } else {
        s_config_pending_persist_mask &= (uint16_t)~debt;
        era_state_sync_note_eeprom_mask(mask);
    }
}
#endif

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
    if (length < ERA_WIRE_VIA_REPORT_SIZE) {
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
    s_config_active_debt_mask        = 0;
    s_config_current_persist_mask    = 0;
    s_config_current_debt_mask       = 0;
#ifdef EEPROM_WEAR_LEVELING
    eeprom_driver_reset_write_observer();
#endif
}
#endif

#ifndef ERA_VIA_SYSTEM_ENABLE
bool via_command_kb(uint8_t *data, uint8_t length) {
    return era_state_sync_via_command(data, length);
}
#endif
