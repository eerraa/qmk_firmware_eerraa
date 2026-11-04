// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "era_macro_options.h"

#include "era_macro.h"
#include "era_common_via.h"
#include "era_state_sync.h"
#include "era_wire.h"
#include "eeprom.h"
#include "../storage/era_eeprom_layout.h"
#include "../storage/era_storage_layout.h"

#ifndef ERA_STORAGE_DYNAMIC_MACRO_EEPROM_SIZE
#    error macro playback options are stored in the storage-adoption geometry; build them through storage/era_storage_adoption_rules.mk
#endif

/* Payloads and refusals are the MACRO_OPTIONS notes of the wire registry
 * (the-via-eerraa/config/era-wire.json); only its numbers are generated. */
#define ERA_MACRO_OPTIONS_SUPPORTED                                                                                    \
    (ERA_WIRE_MACRO_OPTIONS_FLAGS_RETAP | ERA_WIRE_MACRO_OPTIONS_FLAGS_HOLD | ERA_WIRE_MACRO_OPTIONS_FLAGS_RELEASE |    \
     ERA_WIRE_MACRO_OPTIONS_FLAGS_OTHER | ERA_WIRE_MACRO_OPTIONS_FLAGS_BLOCK_OTHERS | ERA_WIRE_MACRO_OPTIONS_FLAGS_IGNORE_MODS)
#define ERA_MACRO_OPTIONS_HOLD_DEFAULT_MS 1000U
/* State Sync names CONFIG spans relative to the ERA block base; the table
 * lies above it, so the same accounting covers it without the 280-byte
 * config accessor, which cannot reach it. */
#define ERA_MACRO_OPTIONS_CONFIG_OFFSET (ERA_STORAGE_MACRO_OPTIONS_ADDR - ERA_EEPROM_CONFIG_ADDR)

_Static_assert(ERA_STORAGE_RECENCY_ADDR + ERA_STORAGE_RECENCY_BYTES <= TOTAL_EEPROM_BYTE_COUNT,
               "The options table and split recency tail must fit the logical EEPROM on every board.");
_Static_assert(DYNAMIC_KEYMAP_MACRO_COUNT <= 32U, "One unsaved bit per macro.");
_Static_assert(!(ERA_MACRO_OPTIONS_SUPPORTED & ERA_WIRE_MACRO_OPTIONS_FLAGS_RESERVED_MASK),
               "Reserved option bits are never supported.");

typedef struct {
    uint8_t  flags;
    uint8_t  repeat_count;
    uint16_t hold_ms;
} era_macro_options_t;

static era_macro_options_t options[DYNAMIC_KEYMAP_MACRO_COUNT];
/* SET changes RAM only. `unsaved` names the records SAVE still owes; without a
 * valid stored header the other records' bytes mean nothing, so that SAVE
 * writes every record and then the header. */
static uint32_t unsaved;
static bool     stored_header;

static void era_macro_options_defaults(void) {
    for (uint8_t i = 0; i < DYNAMIC_KEYMAP_MACRO_COUNT; ++i) {
        options[i].flags   = 0;
        options[i].repeat_count = 1;
        options[i].hold_ms = ERA_MACRO_OPTIONS_HOLD_DEFAULT_MS;
    }
    unsaved       = 0;
    stored_header = false;
}

void era_macro_options_init(void) {
    uint8_t table[ERA_STORAGE_MACRO_OPTIONS_BYTES];
    era_macro_options_defaults();
    eeprom_read_block(table, (const void *)(uintptr_t)ERA_STORAGE_MACRO_OPTIONS_ADDR, sizeof(table));
    const uint32_t signature = (uint32_t)table[0] | (uint32_t)table[1] << 8 | (uint32_t)table[2] << 16 | (uint32_t)table[3] << 24;
    /* A missing, erased or foreign header leaves every record at its default. */
    stored_header = signature == ERA_STORAGE_MACRO_OPTIONS_SIGNATURE && table[4] == ERA_STORAGE_MACRO_OPTIONS_VERSION &&
                    table[5] == DYNAMIC_KEYMAP_MACRO_COUNT && !table[6] && !table[7];
    if (!stored_header) return;
    for (uint8_t i = 0; i < DYNAMIC_KEYMAP_MACRO_COUNT; ++i) {
        const uint8_t *record = &table[ERA_STORAGE_MACRO_OPTIONS_HEADER_BYTES + i * ERA_STORAGE_MACRO_OPTIONS_RECORD_BYTES];
        const uint16_t hold   = (uint16_t)(record[1] | record[2] << 8);
        /* Reserved or unsupported bits, or a zero hold, default that record alone. */
        if ((record[0] & ~ERA_MACRO_OPTIONS_SUPPORTED) || !hold) continue;
        options[i].flags   = record[0];
        options[i].hold_ms = hold;
        options[i].repeat_count = record[3];
    }
}

void era_macro_options_reset_defaults(void) {
    static const uint8_t cleared[ERA_STORAGE_MACRO_OPTIONS_HEADER_BYTES] = {0};
    era_macro_options_defaults();
    /* Inside the clearing initializer's transaction, which owns completion.
     * An already erased table reads cleared, so a formatted store costs no write. */
    eeprom_update_block(cleared, (void *)(uintptr_t)ERA_STORAGE_MACRO_OPTIONS_ADDR, sizeof(cleared));
}

uint8_t era_macro_options_flags(uint8_t id) {
    return id < DYNAMIC_KEYMAP_MACRO_COUNT ? options[id].flags : 0;
}

uint16_t era_macro_options_hold_ms(uint8_t id) {
    return id < DYNAMIC_KEYMAP_MACRO_COUNT ? options[id].hold_ms : ERA_MACRO_OPTIONS_HOLD_DEFAULT_MS;
}

uint8_t era_macro_options_repeat_count(uint8_t id) {
    return id < DYNAMIC_KEYMAP_MACRO_COUNT ? options[id].repeat_count : 1;
}

static bool era_macro_options_persist(void) {
    if (!unsaved) return true;
    const uint32_t records = stored_header ? unsaved : UINT32_MAX;
    if (!eeprom_begin_transaction()) return false;
    era_state_sync_config_persist_begin(ERA_MACRO_OPTIONS_CONFIG_OFFSET, ERA_STORAGE_MACRO_OPTIONS_BYTES);
    for (uint8_t i = 0; i < DYNAMIC_KEYMAP_MACRO_COUNT; ++i) {
        if (!(records & (1UL << i))) continue;
        const uint8_t record[ERA_STORAGE_MACRO_OPTIONS_RECORD_BYTES] = {options[i].flags, (uint8_t)options[i].hold_ms, (uint8_t)(options[i].hold_ms >> 8), options[i].repeat_count};
        eeprom_update_block(record, (void *)(uintptr_t)(ERA_STORAGE_MACRO_OPTIONS_ADDR + ERA_STORAGE_MACRO_OPTIONS_HEADER_BYTES + i * ERA_STORAGE_MACRO_OPTIONS_RECORD_BYTES), sizeof(record));
    }
    if (!stored_header) {
        const uint8_t header[ERA_STORAGE_MACRO_OPTIONS_HEADER_BYTES] = {
            (uint8_t)ERA_STORAGE_MACRO_OPTIONS_SIGNATURE,         (uint8_t)(ERA_STORAGE_MACRO_OPTIONS_SIGNATURE >> 8),
            (uint8_t)(ERA_STORAGE_MACRO_OPTIONS_SIGNATURE >> 16), (uint8_t)(ERA_STORAGE_MACRO_OPTIONS_SIGNATURE >> 24),
            ERA_STORAGE_MACRO_OPTIONS_VERSION,                    DYNAMIC_KEYMAP_MACRO_COUNT,
            0,                                                    0,
        };
        eeprom_update_block(header, (void *)(uintptr_t)ERA_STORAGE_MACRO_OPTIONS_ADDR, sizeof(header));
    }
    const bool ok = eeprom_end_transaction();
    era_state_sync_config_persist_end();
    /* A failed checked SAVE keeps the unsaved intent for the next SAVE. */
    if (ok) {
        unsaved       = 0;
        stored_header = true;
    }
    return ok;
}

static bool era_macro_options_via_save(uint8_t channel_id) {
    (void)channel_id;
    return era_macro_options_persist();
}

static bool era_macro_options_via_set(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    (void)length;
    const uint8_t index = value_data[0];
    if (index >= DYNAMIC_KEYMAP_MACRO_COUNT) return false;
    era_macro_options_t next = options[index];
    switch (value_id) {
        case ERA_WIRE_MACRO_OPTIONS_FLAGS:
            if (value_data[1] & ~ERA_MACRO_OPTIONS_SUPPORTED) return false;
            next.flags = value_data[1];
            break;
        case ERA_WIRE_MACRO_OPTIONS_HOLD_MS:
            next.hold_ms = era_common_via_get_u16_be(&value_data[1]);
            if (!next.hold_ms) return false;
            break;
        case ERA_WIRE_MACRO_OPTIONS_REPEAT_COUNT:
            next.repeat_count = value_data[1];
            break;
        default: /* CAPABILITIES is GET only. */
            return false;
    }
    if (next.flags == options[index].flags && next.hold_ms == options[index].hold_ms && next.repeat_count == options[index].repeat_count) return true;
    options[index] = next;
    unsaved |= 1UL << index;
    era_state_sync_note_config_semantic_commit(ERA_MACRO_OPTIONS_CONFIG_OFFSET, ERA_STORAGE_MACRO_OPTIONS_BYTES);
    return true;
}

static bool era_macro_options_via_get(uint8_t channel_id, uint8_t value_id, uint8_t *value_data, uint8_t length) {
    (void)channel_id;
    (void)length;
    if (value_id == ERA_WIRE_MACRO_OPTIONS_CAPABILITIES) {
        value_data[0] = ERA_WIRE_MACRO_OPTIONS_VERSION_CURRENT;
        value_data[1] = ERA_MACRO_OPTIONS_SUPPORTED;
        value_data[2] = ERA_WIRE_MACRO_OPTIONS_MARKER_BYTE_0;
        value_data[3] = ERA_WIRE_MACRO_OPTIONS_MARKER_BYTE_1;
        value_data[4] = ERA_WIRE_MACRO_OPTIONS_MARKER_BYTE_2;
        value_data[5] = ERA_WIRE_MACRO_OPTIONS_MARKER_BYTE_3;
        return true;
    }
    const uint8_t index = value_data[0];
    if (index >= DYNAMIC_KEYMAP_MACRO_COUNT) return false;
    switch (value_id) {
        case ERA_WIRE_MACRO_OPTIONS_FLAGS:
            value_data[1] = options[index].flags;
            return true;
        case ERA_WIRE_MACRO_OPTIONS_HOLD_MS:
            era_common_via_put_u16_be(&value_data[1], options[index].hold_ms);
            return true;
        case ERA_WIRE_MACRO_OPTIONS_REPEAT_COUNT:
            value_data[1] = options[index].repeat_count;
            return true;
        default:
            return false;
    }
}

bool era_macro_options_handle_via(uint8_t *data, uint8_t length) {
    /* A frame shorter than the report stays unclaimed, which answers id_unhandled. */
    if (length < ERA_WIRE_VIA_REPORT_SIZE || data[1] != ERA_WIRE_CHANNELS_EERRAA_MACRO_OPTIONS) return false;
    return era_common_via_value_command(data, length, era_macro_options_via_save, era_macro_options_via_set, era_macro_options_via_get);
}
